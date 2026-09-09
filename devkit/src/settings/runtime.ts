import { defaultRuntimeExecutable } from "../runtime/session.js";
import type { EntisiumConfig } from "./config.js";
import type { YamlConfigStore } from "./yaml-store.js";

export function runtimeExecutableFromConfig(config: EntisiumConfig | undefined, store: YamlConfigStore): string {
    // Preserve the existing environment override's working-directory semantics.
    if (process.env.ETS_RUNTIME_HOST_PATH?.trim()) return defaultRuntimeExecutable();
    const executable = config?.runtime.executable;
    return executable && executable !== "auto" ? store.resolvePath(executable) : defaultRuntimeExecutable();
}
