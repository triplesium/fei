import { cp, mkdir, readdir, readFile, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { HostProjectService } from "@entisium/devkit/workspace/project-service";
import { RuntimeSession } from "@entisium/devkit/runtime/session";
import { createProjectTools } from "@entisium/agent/tools/project";
import { createNativeAgentTools } from "@entisium/agent/tools/runtime";
import type { EvalCase, Tools } from "./types.js";

export async function readFiles(root: string, prefix = ""): Promise<Record<string, string>> {
    const files: Record<string, string> = {};
    for (const entry of (await readdir(join(root, prefix), { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name))) {
        const path = prefix ? `${prefix}/${entry.name}` : entry.name;
        if (entry.isDirectory()) Object.assign(files, await readFiles(root, path));
        else if (entry.isFile()) files[path] = (await readFile(join(root, path))).toString("base64");
        else throw new Error(`Unexpected non-regular fixture entry: ${path}`);
    }
    return files;
}

export async function createEnvironment(task: EvalCase, root: string) {
    await mkdir(root, { recursive: true });
    if (task.prepare) {
        await task.prepare(root);
    } else if (task.environment.fixture) {
        await cp(task.environment.fixture, root, { recursive: true });
    } else {
        for (const [path, content] of Object.entries(task.files)) {
            const target = join(root, path);
            await mkdir(resolve(target, ".."), { recursive: true });
            await writeFile(target, content);
        }
    }
    const initial = await readFiles(root);
    const native = task.environment.runtime === "native";
    const runtimeInterface = task.runtimeInterface ?? "browser-demo.main";
    const project = new HostProjectService(root);
    const session = new RuntimeSession(undefined, async () => join(root, "project.yaml"));
    const state: Record<string, any> = { position: 0, runtime: "stopped" };
    let cleaned = false;
    const invoke = async (name: string, input: any, signal?: AbortSignal) => {
        if (native) {
            // Capture independently before agent stop destroys the process. No simulation is advanced.
            if (name === "runtime_stop" && session.runtime.status().state === "running") {
                try { state.observation = (await session.invoke("play_observe", { interface: runtimeInterface }, signal)).value; }
                catch (error) { state.observationError = String(error); }
            }
            const result = await session.invoke(name, input, signal);
            if (name === "runtime_play") state.initialObservation = (await session.invoke("play_observe", { interface: runtimeInterface }, signal)).value;
            return result;
        }
        signal?.throwIfAborted();
        switch (name) {
            case "runtime_play":
                if (task.fault === "start") throw new Error("Startup failed. Read retained runtime logs for details.");
                if (state.runtime === "running") throw new Error("Stop the current session first.");
                state.runtime = "running"; state.position = 0; break;
            case "runtime_stop": state.runtime = "stopped"; break;
            case "runtime_status": break;
            case "runtime_logs": return { value: { text: task.fault === "start" ? "E_ASSET_42: required asset missing" : "Ready" } };
            default:
                if (state.runtime !== "running") throw new Error("Runtime is not ready.");
                if (name === "play_interfaces") return { value: { interfaces: [{ id: "counter",
                    action: { type: "object", properties: { delta: { type: "integer" } }, required: ["delta"], additionalProperties: false },
                    observation: { type: "object", properties: { position: { type: "integer" } } },
                    ticks: { min: 1, max: 1, default: 1, overridable: true } }] } };
                if (input.interface !== "counter") throw new Error("Unknown interface.");
                if (name === "play_step") {
                    if (!Number.isInteger(input.action?.delta) || Object.keys(input.action).length !== 1 || (input.ticks ?? 1) !== 1) throw new Error("Expected one tick and integer delta.");
                    state.position += input.action.delta;
                    if (task.fault === "timeout") { state.runtime = "failed"; throw new Error("Action timed out; execution is uncertain. Do not repeat. Stop the session."); }
                } else if (name !== "play_observe") throw new Error("Unsupported operation in this environment.");
                return { value: { observation: { position: state.position } } };
        }
        return { value: { state: state.runtime } };
    };
    let tools: Tools = [
        ...createProjectTools(project).filter(t => task.writable || t.name !== "project_write"),
        ...createNativeAgentTools(invoke).filter(t => native
            ? !["native_runtime_inspect"].includes(t.name)
            : ["native_runtime_play", "native_runtime_stop", "native_runtime_status", "native_runtime_logs", "native_play_interfaces", "native_play_observe", "native_play_step"].includes(t.name)),
    ];
    if (task.fault === "write") tools = tools.map(t => t.name !== "project_write" ? t : {
        ...t, execute: async () => { throw new Error("Write failed: fixture is read-only."); },
    });
    return {
        tools, state, initial,
        async cleanup() {
            if (cleaned) return;
            try {
                state.beforeCleanup = native ? session.runtime.status().state : state.runtime;
                if (native && session.runtime.status().state === "running") {
                    try { state.observation = (await session.invoke("play_observe", { interface: runtimeInterface })).value; }
                    catch (error) { state.observationError = String(error); }
                }
            } finally {
                try { await session.stop(); state.runtime = "stopped"; state.cleaned = true; }
                finally { project.dispose(); cleaned = true; }
            }
        },
    };
}
