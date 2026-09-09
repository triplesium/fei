import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { capabilityInterface } from "../../shared/capability-fixture.js";
import type { Invoke } from "../../harness/types.js";

export async function reference(invoke: Invoke) {
    const source = await readFile(join(taskDirectory("fixed-course-win"), "reference/controller.luau"), "utf8");
    await invoke("native_runtime_play", {});
    await invoke("native_play_interfaces", {});
    await invoke("native_play_observe", { interface: capabilityInterface });
    await invoke("native_play_segment", { interface: capabilityInterface, source, max_ticks: 120 });
    await invoke("native_runtime_stop", {});
    return "Reached victory without collisions and stopped.";
}
