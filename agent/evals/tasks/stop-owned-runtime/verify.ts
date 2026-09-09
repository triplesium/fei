import { join } from "node:path";
import { taskDirectory } from "../../harness/task-definition.js";
import { readTextFixture, check, called, marker } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const base = readTextFixture(join(taskDirectory("stop-owned-runtime"), "project"));
const initial = base;

export const grade: EvalCase["grade"] = (e) => [check("observed initial state", called(e, "native_play_observe") && marker(e.answer, "position=0")), check("no advancement", e.state.position === 0), check("agent stopped runtime", called(e, "native_runtime_stop"))];
