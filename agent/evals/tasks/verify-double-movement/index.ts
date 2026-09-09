import { defineTask, taskDirectory } from "../../harness/task-definition.js";
import { grade, verify } from "./verify.js";
import { reference } from "./reference.js";

export const task = defineTask(taskDirectory("verify-double-movement"), { grade, verify, reference });
