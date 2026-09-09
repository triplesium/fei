import { defineTask, taskDirectory } from "../../harness/task-definition.js";
import { grade, verify } from "./verify.js";
import { reference } from "./reference.js";

export const task = defineTask(taskDirectory("preserve-change-across-turns"), { grade, verify, reference });
