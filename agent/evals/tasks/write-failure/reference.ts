import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("project_read", { path: "assets/settings.json" });
    try {
        await invoke("project_write", { path: "assets/settings.json", content: '{"speed":8}' });
    } catch {
    }
    return "Not saved. saved=false";
};
