import concurrently from "concurrently";
import { resolve } from "node:path";

function projectDirectoryFromArguments(args) {
    for (let index = 0; index < args.length; index += 1) {
        const argument = args[index];
        if (argument === "--project") {
            const value = args[index + 1]?.trim();
            if (!value) throw new Error("--project requires a directory path.");
            return resolve(value);
        }
        if (argument.startsWith("--project=")) {
            const value = argument.slice("--project=".length).trim();
            if (!value) throw new Error("--project requires a directory path.");
            return resolve(value);
        }
        if (argument === "--help" || argument === "-h") {
            console.log("Usage: npm run dev -- [--project <directory>]");
            console.log("       npm run dev:sandbox");
            process.exit(0);
        }
        throw new Error(`Unknown development option: ${argument}`);
    }
    return undefined;
}

const projectDirectory = projectDirectoryFromArguments(process.argv.slice(2));
if (projectDirectory) {
    console.log(`[entisium editor] opening project ${projectDirectory}`);
}

const { result } = concurrently(
    [
        {
            command: "npm run dev:host",
            name: "host",
            env: projectDirectory ? { ETS_EDITOR_PROJECT_DIR: projectDirectory } : undefined,
        },
        {
            command: "npm run dev:renderer",
            name: "renderer",
        },
    ],
    {
        killOthersOn: ["failure", "success"],
        prefix: "name",
    },
);

try {
    await result;
} catch {
    process.exitCode = 1;
}
