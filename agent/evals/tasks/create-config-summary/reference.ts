import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("project_read", { path: "assets/settings.json" });
    await invoke("project_write", { path: "assets/summary.json", content: '{"speed":4,"jump":7}\n' });
    return "Created assets/summary.json.";
};
