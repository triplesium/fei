import { capabilityInterface } from "./capability-fixture.js";
import type { Check, Evidence, Invoke } from "../harness/types.js";

export function verificationChecks(e: Evidence): Check[] {
    const checks = e.verification;
    return Array.isArray(checks) && checks.length ? checks.map(c => ({ ...c, dimension: "outcome" })) : [{ name: "independent verification available", passed: false, dimension: "outcome" }];
}
export function agentVerified(e: Evidence) {
    let lastWrite = -1;
    e.calls.forEach((c, i) => { if (c.name === "project_write" && !c.error) lastWrite = i; });
    const afterWrite = e.calls.slice(lastWrite + 1);
    const start = afterWrite.findIndex(c => c.name === "native_runtime_play" && !c.error);
    return start >= 0 && afterWrite.slice(start + 1).some(c => !c.error && ["native_play_step", "native_play_segment"].includes(c.name));
}
export function agentStopped(e: Evidence) {
    return e.state.beforeCleanup === "stopped" && e.calls.some(c => c.name === "native_runtime_stop" && !c.error);
}
export function noRestartAfterPlay(e: Evidence) {
    let started = false;
    let played = false;
    for (const call of e.calls) {
        if (call.name === "native_runtime_play" && !call.error) {
            if (played) return false;
            started = true;
        }
        // A timed-out action can already have advanced the game; retain that uncertainty.
        if (started && ["native_play_step", "native_play_segment"].includes(call.name)) played = true;
    }
    return started;
}
export async function play(invoke: Invoke) {
    await invoke("native_runtime_play", {});
    await invoke("native_play_interfaces", {});
    await invoke("native_play_observe", { interface: capabilityInterface });
    await invoke("native_play_step", { interface: capabilityInterface, action: { horizontal: 1, jump: true, interact: true }, ticks: 1 });
    await invoke("native_runtime_stop", {});
}


/** Shared constraints for single-turn gameplay edits; task verifiers choose outcome expectations. */
export function gradeGameplayEdit(e: Evidence): Check[] {
    return [...verificationChecks(e),
        { name: "agent verified saved changes", dimension: "verification", passed: agentVerified(e) },
        { name: "only gameplay module edited", dimension: "constraints", passed: !e.calls.some(call => call.name === "project_write" && call.input.path !== "assets/gameplay.luau") },
        { name: "agent stopped runtime", dimension: "constraints", passed: agentStopped(e) },
    ];
}
