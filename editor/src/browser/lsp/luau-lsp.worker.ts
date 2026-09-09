interface LuauProjectFile {
    path: string;
    content: string;
}

interface InitializeMessage {
    type: "initialize";
    moduleUrl: string;
    files: LuauProjectFile[];
    port: MessagePort;
}

interface SyncMessage {
    type: "sync";
    id: number;
    files: LuauProjectFile[];
}

interface EmscriptenFileSystem {
    mkdirTree(path: string): void;
    writeFile(path: string, content: string, options: { encoding: "utf8" }): void;
    unlink(path: string): void;
}

interface EntisiumLspModule {
    FS: EmscriptenFileSystem;
    ccall(
        name: string,
        returnType: null,
        argumentTypes: string[],
        arguments_: unknown[],
    ): void;
    UTF8ToString(pointer: number): string;
    _ets_lsp_start(): number;
    _ets_lsp_take_output(): number;
    _ets_lsp_free(pointer: number): void;
    _ets_lsp_last_error(): number;
}

type EntisiumLspFactory = (options: {
    locateFile(path: string): string;
    print(text: string): void;
    printErr(text: string): void;
}) => Promise<EntisiumLspModule>;

let moduleInstance: EntisiumLspModule | undefined;
let lspPort: MessagePort | undefined;
let outputTimer: ReturnType<typeof setInterval> | undefined;
const synchronizedPaths = new Set<string>();

function projectPath(path: string): string {
    if (
        !path ||
        path.startsWith("/") ||
        path.endsWith("/") ||
        path.includes("\\") ||
        path.split("/").some((part) => !part || part === "." || part === "..")
    ) {
        throw new Error(`Invalid Luau LSP project path: ${path}`);
    }
    return `/workspace/${path}`;
}

function synchronizeFiles(files: readonly LuauProjectFile[]): void {
    if (!moduleInstance) throw new Error("The WebAssembly Luau LSP is not initialized.");
    const nextPaths = new Set<string>();
    for (const file of files) {
        const path = projectPath(file.path);
        const separator = path.lastIndexOf("/");
        moduleInstance.FS.mkdirTree(path.slice(0, separator));
        moduleInstance.FS.writeFile(path, file.content, { encoding: "utf8" });
        nextPaths.add(path);
    }
    for (const path of synchronizedPaths) {
        if (!nextPaths.has(path)) moduleInstance.FS.unlink(path);
    }
    synchronizedPaths.clear();
    for (const path of nextPaths) synchronizedPaths.add(path);
}

function drainOutput(): void {
    if (!moduleInstance || !lspPort) return;
    while (true) {
        const pointer = moduleInstance._ets_lsp_take_output();
        if (pointer === 0) return;
        try {
            lspPort.postMessage(JSON.parse(moduleInstance.UTF8ToString(pointer)));
        } finally {
            moduleInstance._ets_lsp_free(pointer);
        }
    }
}

async function initialize(message: InitializeMessage): Promise<void> {
    const imported = (await import(/* @vite-ignore */ message.moduleUrl)) as {
        default: EntisiumLspFactory;
    };
    const moduleUrl = new URL(message.moduleUrl);
    moduleInstance = await imported.default({
        locateFile: (path) => new URL(path, moduleUrl).href,
        print: (text) => console.info(`[luau-lsp] ${text}`),
        printErr: (text) => console.error(`[luau-lsp] ${text}`),
    });
    moduleInstance.FS.mkdirTree("/workspace");
    synchronizeFiles(message.files);
    if (moduleInstance._ets_lsp_start() !== 0) {
        throw new Error(
            moduleInstance.UTF8ToString(moduleInstance._ets_lsp_last_error()) ||
                "The WebAssembly Luau LSP failed to initialize.",
        );
    }
    lspPort = message.port;
    lspPort.addEventListener("message", (event: MessageEvent<unknown>) => {
        if (
            event.data &&
            typeof event.data === "object" &&
            "method" in event.data &&
            event.data.method === "exit"
        ) {
            return;
        }
        moduleInstance!.ccall(
            "ets_lsp_send",
            null,
            ["string"],
            [JSON.stringify(event.data)],
        );
    });
    lspPort.start();
    outputTimer = setInterval(drainOutput, 8);
}

self.addEventListener(
    "message",
    (event: MessageEvent<InitializeMessage | SyncMessage>) => {
        const message = event.data;
        if (message.type === "initialize") {
            void initialize(message)
                .then(() => self.postMessage({ type: "ready" }))
                .catch((error: unknown) => {
                    if (outputTimer) clearInterval(outputTimer);
                    self.postMessage({
                        type: "error",
                        message: error instanceof Error ? error.message : String(error),
                    });
                });
            return;
        }
        if (message.type === "sync") {
            try {
                synchronizeFiles(message.files);
                self.postMessage({ type: "synced", id: message.id });
            } catch (error) {
                self.postMessage({
                    type: "error",
                    id: message.id,
                    message: error instanceof Error ? error.message : String(error),
                });
            }
        }
    },
);
