import { check, called, findRotation } from "../../shared/project-support.js";
import type { EvalCase } from "../../harness/types.js";

const move = false;

export const grade: EvalCase["grade"] = (e) => {
    const rotation = findRotation(e.state.observation);
    const initialFrame = (e.state.initialObservation as { frame?: number } | undefined)?.frame;
    const finalFrame = (e.state.observation as { frame?: number } | undefined)?.frame;
    return [check("runtime started", called(e, "native_runtime_play")), check("verified runtime observation", (move || called(e, "native_play_observe")) && typeof rotation === "number" && (move ? rotation >= 89 && rotation <= 92 : rotation === findRotation(e.state.initialObservation))), check("requested simulation behavior", move ? called(e, "native_play_step") || called(e, "native_play_segment") : !e.calls.some((c) => ["native_play_step", "native_play_segment"].includes(c.name))), check("requested tick count", typeof initialFrame === "number" && finalFrame === initialFrame + (move ? 1 : 0)), check("agent stopped runtime", called(e, "native_runtime_stop")), check("no file writes", !e.calls.some((c) => c.name === "project_write"))];
};
