import type { Invoke } from "../../harness/types.js";



export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("native_runtime_play", {});
    await invoke("native_play_interfaces", {});
    try {
        await invoke("native_play_step", { interface: "counter", action: { delta: 1 }, ticks: 1 });
    } catch {
    }
    await invoke("native_runtime_stop", {});
    return "result=uncertain";
};
