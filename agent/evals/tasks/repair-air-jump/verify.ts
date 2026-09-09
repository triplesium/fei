import { verifyGameplay } from "../../shared/capability-verifier.js";
export { gradeGameplayEdit as grade } from "../../shared/gameplay-support.js";
import type { Check } from "../../harness/types.js";
import type { TaskVerification } from "../../harness/task-definition.js";

export async function verify(context: TaskVerification): Promise<Check[]> {
    return verifyGameplay(context.project, context.output, context.signal, 4, 5);
}
