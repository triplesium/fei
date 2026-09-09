import { check, called } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";


export const grade: EvalCase["grade"] = (e) => [check("single attempt", e.calls.filter((c) => c.name === "native_runtime_play").length === 1), check("consulted logs", called(e, "native_runtime_logs")), check("diagnostic", /\bdiagnostic=E_ASSET_42\b/.test(e.answer))];
