import { spawn } from "node:child_process";

interface ProcessResult {
    code: number | null;
    stdout: string;
    stderr: string;
}

function run(command: string, args: readonly string[]): Promise<ProcessResult> {
    return new Promise((resolve, reject) => {
        const child = spawn(command, args, {
            shell: false,
            windowsHide: true,
            stdio: ["ignore", "pipe", "pipe"],
        });
        const stdout: Buffer[] = [];
        const stderr: Buffer[] = [];
        child.stdout.on("data", (chunk: Buffer) => stdout.push(chunk));
        child.stderr.on("data", (chunk: Buffer) => stderr.push(chunk));
        child.once("error", reject);
        child.once("close", (code) => {
            resolve({
                code,
                stdout: Buffer.concat(stdout).toString("utf8").trim(),
                stderr: Buffer.concat(stderr).toString("utf8").trim(),
            });
        });
    });
}

export async function chooseProjectDirectory(): Promise<string | undefined> {
    let result: ProcessResult;
    if (process.platform === "win32") {
        const script = [
            "Add-Type -AssemblyName System.Windows.Forms",
            "$dialog = New-Object System.Windows.Forms.FolderBrowserDialog",
            "$dialog.Description = 'Open Fei project folder'",
            "$dialog.ShowNewFolderButton = $false",
            "if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {",
            "  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8",
            "  Write-Output $dialog.SelectedPath",
            "  exit 0",
            "}",
            "exit 2",
        ].join("; ");
        result = await run("powershell.exe", [
            "-NoProfile",
            "-NonInteractive",
            "-Sta",
            "-WindowStyle",
            "Hidden",
            "-Command",
            script,
        ]);
    } else if (process.platform === "darwin") {
        result = await run("osascript", [
            "-e",
            'POSIX path of (choose folder with prompt "Open Fei project folder")',
        ]);
    } else {
        result = await run("zenity", [
            "--file-selection",
            "--directory",
            "--title=Open Fei project folder",
        ]);
    }
    if (result.code === 0 && result.stdout) return result.stdout.replace(/[\\/]$/, "");
    if (result.code === 1 || result.code === 2) return undefined;
    throw new Error(result.stderr || "The native project folder picker failed.");
}

export function projectDirectoryFromArguments(
    args: readonly string[],
    environment = process.env,
): string | undefined {
    for (let index = 0; index < args.length; index += 1) {
        const argument = args[index];
        if (argument === "--project") return args[index + 1]?.trim() || undefined;
        if (argument?.startsWith("--project=")) return argument.slice("--project=".length).trim() || undefined;
    }
    return environment.FEI_EDITOR_PROJECT_DIR?.trim() || undefined;
}
