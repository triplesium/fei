import { check, called, marker } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";


export const grade: EvalCase["grade"] = (e) => [check("one action only", e.calls.filter((c) => c.name === "native_play_step").length === 1 && e.state.position === 1), check("no restart", e.calls.filter((c) => c.name === "native_runtime_play").length === 1), check("reports uncertainty", marker(e.answer, "result=uncertain")), check("agent stopped runtime", called(e, "native_runtime_stop"))];
