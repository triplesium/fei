import { defineTask, taskDirectory } from "../../harness/task-definition.js";
import { grade } from "./verify.js";
import { reference } from "./reference.js";

export const task = defineTask(taskDirectory("stop-owned-runtime"), { grade, reference });
