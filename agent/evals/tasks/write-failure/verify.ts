import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { readTextFixture, check, marker, unchanged } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const base = readTextFixture(join(taskDirectory("write-failure"), "project"));

export const grade: EvalCase["grade"] = (e) => [check("attempted write", e.calls.filter((c) => c.name === "project_write").length === 1), check("no false success", marker(e.answer, "saved=false")), check("unchanged after failure", unchanged(base, e))];
