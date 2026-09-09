import { agentStopped, noRestartAfterPlay } from "../../shared/gameplay-support.js";
import type { Check, Evidence } from "../../harness/types.js";

export function grade(e: Evidence): Check[] {
    const state = (e.state.observation as { observation?: { won: number; x: number; collisions: number; ticks: number } } | undefined)?.observation;
    return [
        { name: "actual victory", dimension: "outcome", passed: state?.won === 1 && state.x >= 3 },
        { name: "collision budget", dimension: "outcome", passed: state?.collisions === 0 },
        { name: "tick budget", dimension: "outcome", passed: !!state && state.ticks > 0 && state.ticks <= 180 },
        { name: "no restart after gameplay", dimension: "constraints", passed: noRestartAfterPlay(e) },
        { name: "agent stopped runtime", passed: agentStopped(e) },
    ];
}
