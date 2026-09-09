import { verifyGameplay } from "../../shared/capability-verifier.js";
import type { TaskVerification } from "../../harness/task-definition.js";

export { gradeGameplayEdit as grade } from "../../shared/gameplay-support.js";

export function verify(context: TaskVerification) {
    return verifyGameplay(context.project, context.output, context.signal, 8, 5);
}
