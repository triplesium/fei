import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("native_runtime_play", {});
    await invoke("native_play_interfaces", {});
    await invoke("native_play_observe", { interface: "counter" });
    await invoke("native_runtime_stop", {});
    return "position=0";
};
