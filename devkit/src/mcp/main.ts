import { runtimeExecutableFromConfig } from "../settings/runtime.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { NativeRuntime } from "../runtime/native-runtime.js";
import { createRuntimeMcp } from "./runtime.js";
import { loadConfig } from "../settings/yaml-store.js";

const { store: settings, config } = await loadConfig();
const runtime = new NativeRuntime(runtimeExecutableFromConfig(config, settings));
const server = createRuntimeMcp(runtime);
let shuttingDown = false;
async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    try { await runtime.stop(); await server.close(); }
    catch (error) { console.error(error); process.exitCode = 1; }
}
process.once("SIGINT", () => { void shutdown(); });
process.once("SIGTERM", () => { void shutdown(); });
process.stdin.once("end", () => { void shutdown(); });
server.server.onclose = () => { void shutdown(); };
await server.connect(new StdioServerTransport());
