import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { play } from "../../shared/gameplay-support.js";
import type { Invoke } from "../../harness/types.js";

export async function reference(invoke: Invoke, step: string) {
    const content = await readFile(join(taskDirectory("repair-luau-startup"), "reference", `${step}.luau`), "utf8");
    await invoke("project_write", { path: "assets/gameplay.luau", content });
    await play(invoke);
    return "Saved the change, verified gameplay and stopped the runtime.";
}
