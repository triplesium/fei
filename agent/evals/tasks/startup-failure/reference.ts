import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    try {
        await invoke("native_runtime_play", {});
    } catch {
    }
    await invoke("native_runtime_logs", {});
    return "diagnostic=E_ASSET_42";
};
