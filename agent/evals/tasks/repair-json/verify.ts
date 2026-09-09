import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { readTextFixture, check, unchanged } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const base = readTextFixture(join(taskDirectory("repair-json"), "project"));
const initial = base;
const expected = {"speed":4,"jump":7,"lives":3,"sound":true};

export const grade: EvalCase["grade"] = (e) => {
    let value;
    try {
        value = JSON.parse(e.files["assets/settings.json"]);
    } catch {
    }
    return [check("requested configuration", !!value && Object.keys(value).length === Object.keys(expected).length && Object.entries(expected).every(([k, v]) => value[k] === v)), check("other files preserved", unchanged({ ...initial, "assets/settings.json": e.files["assets/settings.json"] }, e)), check("no runtime start", !e.calls.some((c) => c.name === "native_runtime_play"))];
};
