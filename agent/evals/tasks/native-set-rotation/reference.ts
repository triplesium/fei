import type { Invoke } from "../../harness/types.js";

const move = true;

export const reference: (invoke: Invoke) => Promise<string> = async (invoke) => {
    await invoke("native_runtime_play", {});
    await invoke("native_play_interfaces", {});
    await invoke("native_play_observe", { interface: "browser-demo.main" });
    if (move) await invoke("native_play_step", { interface: "browser-demo.main", action: { rotation: 90 }, ticks: 1 });
    await invoke("native_play_observe", { interface: "browser-demo.main" });
    await invoke("native_runtime_stop", {});
    return "Verified native rotation and stopped.";
};
