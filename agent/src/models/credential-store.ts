import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { spawn } from "node:child_process";
import type {
    AuthOperationOptions,
    Credential,
    CredentialInfo,
    CredentialStore,
} from "@earendil-works/pi-ai";

interface StoredCredentialFile {
    version: 1;
    protected: string;
}

export interface SecretProtector {
    protect(plaintext: string): Promise<string>;
    unprotect(ciphertext: string): Promise<string>;
}

const entropyLabel = "EntisiumEditorCredentials/v1";

const protectScript = `
Add-Type -AssemblyName System.Security
$inputValue = [Console]::In.ReadToEnd().Trim()
$plaintext = [Convert]::FromBase64String($inputValue)
$entropy = [Text.Encoding]::UTF8.GetBytes('${entropyLabel}')
$ciphertext = [Security.Cryptography.ProtectedData]::Protect(
    $plaintext,
    $entropy,
    [Security.Cryptography.DataProtectionScope]::CurrentUser
)
[Console]::Out.Write([Convert]::ToBase64String($ciphertext))
`;

const unprotectScript = `
Add-Type -AssemblyName System.Security
$inputValue = [Console]::In.ReadToEnd().Trim()
$ciphertext = [Convert]::FromBase64String($inputValue)
$entropy = [Text.Encoding]::UTF8.GetBytes('${entropyLabel}')
$plaintext = [Security.Cryptography.ProtectedData]::Unprotect(
    $ciphertext,
    $entropy,
    [Security.Cryptography.DataProtectionScope]::CurrentUser
)
[Console]::Out.Write([Convert]::ToBase64String($plaintext))
`;

function encodedPowerShellCommand(script: string): string {
    return Buffer.from(script, "utf16le").toString("base64");
}

function runPowerShell(script: string, input: string): Promise<string> {
    return new Promise((resolve, reject) => {
        const systemRoot = process.env.SystemRoot?.trim() || "C:\\Windows";
        const powerShell = join(
            systemRoot,
            "System32",
            "WindowsPowerShell",
            "v1.0",
            "powershell.exe",
        );
        const child = spawn(
            powerShell,
            ["-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encodedPowerShellCommand(script)],
            { stdio: ["pipe", "pipe", "pipe"], windowsHide: true },
        );
        const stdout: Buffer[] = [];
        const stderr: Buffer[] = [];
        child.stdout.on("data", (chunk: Buffer) => stdout.push(chunk));
        child.stderr.on("data", (chunk: Buffer) => stderr.push(chunk));
        child.on("error", reject);
        child.on("close", (code) => {
            if (code !== 0) {
                reject(
                    new Error(
                        `Windows credential protection failed (${code ?? "unknown"}): ${Buffer.concat(stderr).toString("utf8").trim()}`,
                    ),
                );
                return;
            }
            resolve(Buffer.concat(stdout).toString("utf8").trim());
        });
        child.stdin.end(input);
    });
}

export class WindowsDpapiProtector implements SecretProtector {
    async protect(plaintext: string): Promise<string> {
        if (process.platform !== "win32") {
            throw new Error("Windows DPAPI credential storage is only available on Windows.");
        }
        return runPowerShell(protectScript, Buffer.from(plaintext, "utf8").toString("base64"));
    }

    async unprotect(ciphertext: string): Promise<string> {
        if (process.platform !== "win32") {
            throw new Error("Windows DPAPI credential storage is only available on Windows.");
        }
        const plaintext = await runPowerShell(unprotectScript, ciphertext);
        return Buffer.from(plaintext, "base64").toString("utf8");
    }
}

function cloneCredential(credential: Credential | undefined): Credential | undefined {
    return credential === undefined ? undefined : structuredClone(credential);
}

function parseCredentials(value: unknown): Map<string, Credential> {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Credential data is not an object.");
    }
    const credentials = new Map<string, Credential>();
    for (const [providerId, candidate] of Object.entries(value)) {
        if (
            !candidate ||
            typeof candidate !== "object" ||
            Array.isArray(candidate) ||
            !("type" in candidate) ||
            (candidate.type !== "api_key" && candidate.type !== "oauth")
        ) {
            throw new Error(`Credential data for ${providerId} is invalid.`);
        }
        credentials.set(providerId, candidate as Credential);
    }
    return credentials;
}

export function defaultCredentialPath(): string {
    return defaultCredentialPaths()[0];
}

export function defaultCredentialPaths(): readonly string[] {
    const configuredPath = process.env.ETS_EDITOR_CREDENTIAL_PATH?.trim();
    if (configuredPath) return [resolve(configuredPath)];

    const applicationData = process.env.APPDATA?.trim();
    const localApplicationData = process.env.LOCALAPPDATA?.trim();
    const roamingRoot = applicationData || join(homedir(), "AppData", "Roaming");
    const localRoot = localApplicationData || join(homedir(), "AppData", "Local");
    return Array.from(
        new Set([
            join(roamingRoot, "Entisium", "editor-credentials.json"),
            join(localRoot, "Entisium", "editor-credentials.json"),
            resolve(process.cwd(), ".entisium", "editor-credentials.json"),
            fileURLToPath(new URL("../../../editor/.entisium/editor-credentials.json", import.meta.url)),
        ]),
    );
}

function isUnavailablePathError(error: unknown): boolean {
    const code = (error as NodeJS.ErrnoException).code;
    return (
        code === "EACCES" ||
        code === "EPERM" ||
        code === "EROFS" ||
        code === "EEXIST" ||
        code === "ENOTDIR"
    );
}

export class EncryptedCredentialStore implements CredentialStore {
    private credentialsPromise: Promise<Map<string, Credential>> | undefined;
    private mutationChain: Promise<void> = Promise.resolve();
    private readonly paths: readonly string[];
    private readonly unreadablePaths = new Set<string>();
    private activePath: string | undefined;

    constructor(
        path: string | readonly string[] = defaultCredentialPaths(),
        private readonly protector: SecretProtector = new WindowsDpapiProtector(),
    ) {
        this.paths = typeof path === "string" ? [path] : Array.from(path);
        if (this.paths.length === 0) throw new Error("At least one credential path is required.");
    }

    async read(providerId: string, options?: AuthOperationOptions): Promise<Credential | undefined> {
        options?.signal?.throwIfAborted();
        await this.mutationChain;
        const credentials = await this.load();
        options?.signal?.throwIfAborted();
        return cloneCredential(credentials.get(providerId));
    }

    async list(options?: AuthOperationOptions): Promise<readonly CredentialInfo[]> {
        options?.signal?.throwIfAborted();
        await this.mutationChain;
        const credentials = await this.load();
        return Array.from(credentials, ([providerId, credential]) => ({
            providerId,
            type: credential.type,
        }));
    }

    modify(
        providerId: string,
        fn: (current: Credential | undefined) => Promise<Credential | undefined>,
        options?: AuthOperationOptions,
    ): Promise<Credential | undefined> {
        return this.enqueue(async () => {
            options?.signal?.throwIfAborted();
            const credentials = await this.load();
            const next = await fn(cloneCredential(credentials.get(providerId)));
            options?.signal?.throwIfAborted();
            if (next !== undefined) {
                credentials.set(providerId, structuredClone(next));
                await this.persist(credentials);
            }
            return cloneCredential(credentials.get(providerId));
        });
    }

    delete(providerId: string, options?: AuthOperationOptions): Promise<void> {
        return this.enqueue(async () => {
            options?.signal?.throwIfAborted();
            const credentials = await this.load();
            if (!credentials.delete(providerId)) return;
            await this.persist(credentials);
        });
    }

    private enqueue<T>(operation: () => Promise<T>): Promise<T> {
        const result = this.mutationChain.then(operation, operation);
        this.mutationChain = result.then(
            () => undefined,
            () => undefined,
        );
        return result;
    }

    private load(): Promise<Map<string, Credential>> {
        this.credentialsPromise ??= this.loadFromDisk();
        return this.credentialsPromise;
    }

    private async loadFromDisk(): Promise<Map<string, Credential>> {
        for (const path of this.paths) {
            let source: string;
            try {
                source = await readFile(path, "utf8");
            } catch (error) {
                if ((error as NodeJS.ErrnoException).code === "ENOENT" || isUnavailablePathError(error)) {
                    continue;
                }
                throw error;
            }
            const file = JSON.parse(source) as Partial<StoredCredentialFile>;
            if (file.version !== 1 || typeof file.protected !== "string") {
                throw new Error("Credential file has an unsupported format.");
            }
            let plaintext: string;
            try {
                plaintext = await this.protector.unprotect(file.protected);
            } catch {
                this.unreadablePaths.add(path);
                console.warn(
                    `[entisium editor] credential file cannot be decrypted by the current user; skipping: ${path}`,
                );
                continue;
            }
            this.activePath = path;
            return parseCredentials(JSON.parse(plaintext));
        }
        return new Map();
    }

    private async persist(credentials: Map<string, Credential>): Promise<void> {
        const plaintext = JSON.stringify(Object.fromEntries(credentials));
        const file: StoredCredentialFile = {
            version: 1,
            protected: await this.protector.protect(plaintext),
        };

        const candidates = this.activePath
            ? [this.activePath]
            : this.paths.filter((path) => !this.unreadablePaths.has(path));
        let lastError: unknown;
        for (const path of candidates) {
            const temporaryPath = `${path}.${process.pid}.${randomUUID()}.tmp`;
            try {
                await mkdir(dirname(path), { recursive: true });
                await writeFile(temporaryPath, JSON.stringify(file), "utf8");
                await rename(temporaryPath, path);
                this.activePath = path;
                return;
            } catch (error) {
                await unlink(temporaryPath).catch(() => undefined);
                lastError = error;
                if (this.activePath || !isUnavailablePathError(error)) throw error;
                console.warn(`[entisium editor] credential path is unavailable: ${path}`);
            }
        }
        throw lastError ?? new Error("No writable credential path is available.");
    }
}
