import type { Invoke } from "../../harness/types.js";


const expected = {"speed":4,"jump":7,"lives":3,"sound":false};

export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("project_read", { path: "assets/settings.json" });
    await invoke("project_write", { path: "assets/settings.json", content: JSON.stringify(expected, null, 2) + "\n" });
    return "Updated assets/settings.json.";
};
