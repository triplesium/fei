import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { readTextFixture, check, unchanged } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const base = readTextFixture(join(taskDirectory("create-config-summary"), "project"));

export const grade: EvalCase["grade"] = (e) => {
    let value;
    try {
        value = JSON.parse(e.files["assets/summary.json"]);
    } catch {
    }
    return [check("summary values", value?.speed === 4 && value?.jump === 7 && Object.keys(value).length === 2), check("existing files preserved", unchanged({ ...base, "assets/summary.json": e.files["assets/summary.json"] }, e)), check("no runtime start", !e.calls.some((c) => c.name === "native_runtime_play"))];
};
