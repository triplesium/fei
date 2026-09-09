import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { readTextFixture, check, called, marker, unchanged } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const base = readTextFixture(join(taskDirectory("read-only-speed"), "project"));

export const grade: EvalCase["grade"] = (e) => [check("correct grounded value", marker(e.answer, "speed=4") && called(e, "project_read")), check("no changes", unchanged(base, e)), check("no runtime start", !e.calls.some((c) => c.name === "native_runtime_play"))];
