import { resolve } from "node:path";
import { EncryptedCredentialStore } from "./credential-store.js";
import {
    chooseProjectDirectory,
    projectDirectoryFromArguments,
} from "./native-directory-picker.js";
import { createEditorHost } from "./server.js";

function portFromEnvironment(): number {
    const value = Number.parseInt(process.env.FEI_EDITOR_HOST_PORT ?? "3100", 10);
    if (!Number.isInteger(value) || value < 1 || value > 65_535) {
        throw new Error("FEI_EDITOR_HOST_PORT must be a valid TCP port.");
    }
    return value;
}

const host = createEditorHost({
    credentials: new EncryptedCredentialStore(),
    distDirectory: resolve(process.cwd(), "dist"),
    runtimeDirectory: resolve(
        process.env.FEI_EDITOR_RUNTIME_DIR ??
            resolve(process.cwd(), "..", "build", "wasm", "wasm32", "debug"),
    ),
    host: "127.0.0.1",
    port: portFromEnvironment(),
    projectDirectory: projectDirectoryFromArguments(process.argv.slice(2)),
    pickProjectDirectory: chooseProjectDirectory,
});

const address = await host.listen();
console.log(`[fei editor] local host listening on http://${address.host}:${address.port}`);

function shutdown(): void {
    host.server.close(() => process.exit(0));
    host.server.closeAllConnections();
}

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
