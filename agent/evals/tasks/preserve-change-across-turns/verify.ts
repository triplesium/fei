import { verifyGameplay } from "../../shared/capability-verifier.js";
import { verificationChecks, agentVerified, agentStopped } from "../../shared/gameplay-support.js";
import type { Evidence, Check } from "../../harness/types.js";
import type { TaskVerification } from "../../harness/task-definition.js";
import { join } from "node:path";

export async function verify(context: TaskVerification): Promise<Check[]> {
    const checks = await verifyGameplay(context.project, context.output, context.signal, 8, 6);
    try {
        const first = await verifyGameplay(context.steps.movement, join(context.output, "first-turn"), context.signal, 8, 5);
        checks.push(...first.map(check => ({ ...check, name: `first turn: ${check.name}` })));
    } catch (error) {
        context.signal.throwIfAborted();
        checks.push({ name: "first turn: snapshot verification", passed: false, detail: String(error) });
    }
    return checks;
}

export function grade(e: Evidence): Check[] {
    return [...verificationChecks(e),
        { name: "agent verified saved changes", dimension: "verification", passed: [0, 1].every(turn => agentVerified({ ...e, calls: e.calls.filter(call => call.turn === turn) })) },
        { name: "only gameplay module edited", dimension: "constraints", passed: !e.calls.some(call => call.name === "project_write" && call.input.path !== "assets/gameplay.luau") },
        { name: "agent stopped runtime", dimension: "constraints", passed: agentStopped(e) },
    ];
}
