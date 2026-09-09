import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("project_read", { path: "assets/settings.json" });
    return "speed=4";
};
