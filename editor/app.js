"use strict";

const ui = {
    projectName: document.querySelector("#project-name"),
    openFolder: document.querySelector("#open-folder"),
    save: document.querySelector("#save"),
    play: document.querySelector("#play"),
    stop: document.querySelector("#stop"),
    restart: document.querySelector("#restart"),
    newFile: document.querySelector("#new-file"),
    renameFile: document.querySelector("#rename-file"),
    deleteFile: document.querySelector("#delete-file"),
    refreshProject: document.querySelector("#refresh-project"),
    fileOperation: document.querySelector("#file-operation"),
    fileOperationLabel: document.querySelector("#file-operation-label"),
    fileOperationPath: document.querySelector("#file-operation-path"),
    fileOperationError: document.querySelector("#file-operation-error"),
    cancelFileOperation: document.querySelector("#cancel-file-operation"),
    runtimeState: document.querySelector("#runtime-state"),
    fileTree: document.querySelector("#file-tree"),
    storageKind: document.querySelector("#storage-kind"),
    saveState: document.querySelector("#save-state"),
    dirtyIndicator: document.querySelector("#dirty-indicator"),
    activePath: document.querySelector("#active-path"),
    languageLabel: document.querySelector("#language-label"),
    editor: document.querySelector("#source-editor"),
    lineNumbers: document.querySelector("#line-numbers"),
    cursorPosition: document.querySelector("#cursor-position"),
    inspectorState: document.querySelector("#inspector-state"),
    inspectorScript: document.querySelector("#inspector-script"),
    inspectorFrame: document.querySelector("#inspector-frame"),
    agentCapabilities: document.querySelector("#agent-capabilities"),
    previewStatus: document.querySelector("#preview-status"),
    runtimeStage: document.querySelector("#runtime-stage"),
    runtimePlaceholder: document.querySelector("#runtime-placeholder"),
    consoleOutput: document.querySelector("#console-output"),
    clearConsole: document.querySelector("#clear-console"),
};

class ProjectStorage {
    constructor() {
        this.root = null;
        this.pendingRoot = null;
        this.kind = "Local folder";
    }

    async initialize() {
        if (typeof window.showDirectoryPicker !== "function") {
            throw new Error("The local folder editor requires a current version of Edge or Chrome.");
        }
        try {
            const root = await this.loadRememberedRoot();
            if (!root || root.kind !== "directory") return null;
            const permission = await root.queryPermission({ mode: "readwrite" });
            if (permission === "granted") {
                await this.attach(root);
                return { name: root.name, restored: true };
            }
            this.pendingRoot = root;
            return { name: root.name, permissionRequired: true };
        } catch (error) {
            console.warn("[fei editor] could not restore the last project folder", error);
            return null;
        }
    }

    async open() {
        if (this.pendingRoot) {
            const root = this.pendingRoot;
            const permission = await root.requestPermission({ mode: "readwrite" });
            if (permission !== "granted") {
                throw new Error(`Access to ${root.name} was not granted.`);
            }
            await this.attach(root);
            return root.name;
        }
        const root = await window.showDirectoryPicker({ mode: "readwrite" });
        await this.attach(root);
        await this.rememberRoot(root);
        return root.name;
    }

    async attach(root) {
        try {
            await root.getFileHandle("project.yaml");
        } catch (error) {
            if (error?.name === "NotFoundError") {
                throw new Error("Select a project folder containing project.yaml.");
            }
            throw error;
        }
        this.root = root;
        this.pendingRoot = null;
    }

    async database() {
        return new Promise((resolve, reject) => {
            const request = indexedDB.open("fei-web-editor", 1);
            request.addEventListener("upgradeneeded", () => {
                if (!request.result.objectStoreNames.contains("settings")) {
                    request.result.createObjectStore("settings");
                }
            });
            request.addEventListener("success", () => resolve(request.result));
            request.addEventListener("error", () => reject(request.error));
        });
    }

    async storedValue(mode, operation) {
        const database = await this.database();
        try {
            return await new Promise((resolve, reject) => {
                const transaction = database.transaction("settings", mode);
                const request = operation(transaction.objectStore("settings"));
                request.addEventListener("success", () => resolve(request.result));
                request.addEventListener("error", () => reject(request.error));
                transaction.addEventListener("abort", () => reject(transaction.error));
            });
        } finally {
            database.close();
        }
    }

    loadRememberedRoot() {
        return this.storedValue("readonly", (store) => store.get("last-project-root"));
    }

    rememberRoot(root) {
        return this.storedValue("readwrite", (store) => store.put(root, "last-project-root"));
    }

    assertOpen() {
        if (!this.root) throw new Error("Open a local project folder first.");
    }

    validatePath(path) {
        if (
            typeof path !== "string" ||
            path.length === 0 ||
            path.startsWith("/") ||
            path.endsWith("/") ||
            path.includes("\\") ||
            path.split("/").some((part) => part.length === 0 || part === "." || part === "..")
        ) {
            throw new Error("Path must be a valid relative project path.");
        }
        if (path !== "project.yaml" && !path.startsWith("assets/")) {
            throw new Error("Project files must be project.yaml or inside assets/.");
        }
        return path;
    }

    async fileHandle(path, create) {
        this.assertOpen();
        this.validatePath(path);
        const parts = path.split("/");
        const fileName = parts.pop();
        let directory = this.root;
        for (const part of parts) {
            directory = await directory.getDirectoryHandle(part, { create });
        }
        return directory.getFileHandle(fileName, { create });
    }

    async read(path) {
        this.validatePath(path);
        try {
            const handle = await this.fileHandle(path, false);
            return (await handle.getFile()).text();
        } catch (error) {
            if (error?.name === "NotFoundError") return null;
            throw error;
        }
    }

    async write(path, content) {
        this.validatePath(path);
        if (typeof content !== "string") {
            throw new Error("Only UTF-8 text project files can be written.");
        }
        const handle = await this.fileHandle(path, true);
        const writable = await handle.createWritable();
        await writable.write(content);
        await writable.close();
    }

    async list() {
        this.assertOpen();
        const files = [];
        const visit = async (directory, prefix = "") => {
            for await (const [name, handle] of directory.entries()) {
                const path = prefix ? `${prefix}/${name}` : name;
                if (handle.kind === "directory") {
                    await visit(handle, path);
                } else {
                    const text = /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|json|lua|luau|md|slang|txt|wgsl|ya?ml)$/i.test(path);
                    files.push({ path, kind: text ? "text" : "binary", readonly: !text });
                }
            }
        };
        files.push({ path: "project.yaml", kind: "text", readonly: false });
        try {
            await visit(await this.root.getDirectoryHandle("assets"), "assets");
        } catch (error) {
            if (error?.name !== "NotFoundError") throw error;
        }
        return files.sort((left, right) => left.path.localeCompare(right.path));
    }

    async exists(path) {
        return (await this.read(path)) !== null;
    }

    async remove(path) {
        this.validatePath(path);
        this.assertOpen();
        const parts = path.split("/");
        const fileName = parts.pop();
        let directory = this.root;
        for (const part of parts) {
            directory = await directory.getDirectoryHandle(part);
        }
        await directory.removeEntry(fileName);
    }

    async rename(source, destination) {
        this.validatePath(source);
        this.validatePath(destination);
        if (source === destination) return;
        if (await this.exists(destination)) {
            throw new Error(`Project file already exists: ${destination}`);
        }
        const content = await this.read(source);
        if (content === null) throw new Error(`Project file not found: ${source}`);
        await this.write(destination, content);
        await this.remove(source);
    }

    async bytes(path) {
        const handle = await this.fileHandle(path, false);
        return new Uint8Array(await (await handle.getFile()).arrayBuffer());
    }
}

const storage = new ProjectStorage();
const state = {
    activePath: "",
    savedContent: "",
    files: [],
    fileOperation: null,
    runtime: "stopped",
    iframe: null,
    channelId: null,
    statusTimer: null,
    startTimeout: null,
    messageController: null,
};

function requestId() {
    return globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
}

function appendConsole(level, source, message) {
    const line = document.createElement("div");
    line.className = `console-line ${level}`;
    const time = new Date().toLocaleTimeString([], {
        hour12: false,
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
    });
    for (const [className, text] of [
        ["time", time],
        ["source", source],
        ["message", String(message)],
    ]) {
        const span = document.createElement("span");
        span.className = className;
        span.textContent = text;
        line.append(span);
    }
    ui.consoleOutput.append(line);
    ui.consoleOutput.scrollTop = ui.consoleOutput.scrollHeight;
}

function updateLineNumbers() {
    const count = ui.editor.value.split("\n").length;
    ui.lineNumbers.textContent = Array.from(
        { length: count },
        (_, index) => index + 1,
    ).join("\n");
    ui.lineNumbers.scrollTop = ui.editor.scrollTop;
}

function updateCursor() {
    const prefix = ui.editor.value.slice(0, ui.editor.selectionStart);
    const lines = prefix.split("\n");
    ui.cursorPosition.textContent = `Ln ${lines.length}, Col ${lines.at(-1).length + 1}`;
}

function updateDirtyState() {
    if (!storage.root || !state.activePath) {
        ui.dirtyIndicator.classList.remove("visible");
        ui.saveState.textContent = "—";
        return;
    }
    const dirty = ui.editor.value !== state.savedContent;
    ui.dirtyIndicator.classList.toggle("visible", dirty);
    ui.saveState.textContent = dirty ? "Unsaved" : "Saved";
    ui.saveState.classList.toggle("dirty", dirty);
}

function setRuntimeState(value, detail = value) {
    state.runtime = value;
    ui.runtimeState.className = `runtime-state ${value}`;
    ui.runtimeState.lastElementChild.textContent =
        value.charAt(0).toUpperCase() + value.slice(1);
    ui.inspectorState.textContent = detail;
    ui.previewStatus.textContent = detail;
    ui.play.disabled = !storage.root || value === "starting" || value === "running";
    ui.stop.disabled = value === "stopped";
    ui.restart.disabled = value !== "running" && value !== "failed";
    document.documentElement.dataset.feiEditorRuntime = value;
}

function fileIcon(entry) {
    if (entry.kind === "binary") return "▧";
    if (entry.path.endsWith(".luau")) return "λ";
    if (entry.path.endsWith(".yaml") || entry.path.endsWith(".yml")) return "◇";
    if (entry.path.endsWith(".json")) return "{}";
    return "·";
}

function updateFileActions() {
    const hasProject = Boolean(storage.root);
    const protectedFile = state.activePath === "project.yaml";
    ui.save.disabled = !hasProject || !state.activePath;
    ui.editor.disabled = !hasProject || !state.activePath;
    ui.newFile.disabled = !hasProject;
    ui.refreshProject.disabled = !hasProject;
    ui.renameFile.disabled = !hasProject || !state.activePath || protectedFile;
    ui.deleteFile.disabled = !hasProject || !state.activePath || protectedFile;
}

async function refreshFiles() {
    if (!storage.root) {
        state.files = [];
        renderFileTree();
        updateFileActions();
        return;
    }
    state.files = (await storage.list()).sort((left, right) => {
        if (left.path === "project.yaml") return -1;
        if (right.path === "project.yaml") return 1;
        return left.path.localeCompare(right.path);
    });
    renderFileTree();
    updateFileActions();
}

function renderFileTree() {
    ui.fileTree.replaceChildren();
    if (!storage.root) {
        const empty = document.createElement("div");
        empty.className = "tree-empty";
        empty.textContent = "Open a local project folder to begin.";
        ui.fileTree.append(empty);
        return;
    }
    const renderedDirectories = new Set();
    for (const entry of state.files) {
        const parts = entry.path.split("/");
        const label = parts.pop();
        const directory = parts.join("/");
        if (directory && !renderedDirectories.has(directory)) {
            const group = document.createElement("div");
            group.className = "tree-group";
            group.textContent = `⌄ ${directory}`;
            ui.fileTree.append(group);
            renderedDirectories.add(directory);
        }
        const button = document.createElement("button");
        button.type = "button";
        button.className = [
            "file-entry",
            directory ? "nested" : "",
            entry.path === state.activePath ? "active" : "",
            entry.readonly ? "readonly" : "",
        ]
            .filter(Boolean)
            .join(" ");
        button.style.setProperty("--tree-depth", String(parts.length));
        button.innerHTML = `<span class="file-icon"></span><span></span>`;
        button.firstElementChild.textContent = fileIcon(entry);
        button.lastElementChild.textContent = label;
        if (!entry.readonly) {
            button.addEventListener("click", () => openFile(entry.path));
        }
        ui.fileTree.append(button);
    }
}

async function openFile(path) {
    if (path === state.activePath) return;
    if (ui.editor.value !== state.savedContent) await saveActiveFile();
    state.activePath = path;
    state.savedContent = (await storage.read(path)) ?? "";
    ui.editor.value = state.savedContent;
    ui.activePath.textContent = path;
    ui.languageLabel.textContent = languageForPath(path);
    renderFileTree();
    updateFileActions();
    updateLineNumbers();
    updateCursor();
    updateDirtyState();
}

function newFileContent(path) {
    if (path.endsWith(".luau")) {
        const moduleName = path
            .replace(/^assets\//, "")
            .replace(/\.luau$/, "")
            .replaceAll("/", ".");
        return `return module {\n    name = "${moduleName}",\n}\n`;
    }
    if (path.endsWith(".json")) return "{}\n";
    return "";
}

function assertMutablePath(path) {
    storage.validatePath(path);
    if (path === "project.yaml") {
        throw new Error("project.yaml cannot be renamed or deleted.");
    }
}

async function createProjectFile(path, content = newFileContent(path), select = false) {
    storage.validatePath(path);
    if (await storage.exists(path)) throw new Error(`Project file already exists: ${path}`);
    await storage.write(path, content);
    await refreshFiles();
    if (select) {
        state.activePath = "";
        await openFile(path);
    }
    return { path, created: true };
}

async function renameProjectFile(source, destination) {
    assertMutablePath(source);
    storage.validatePath(destination);
    if (source === state.activePath && ui.editor.value !== state.savedContent) {
        await saveActiveFile();
    }
    await storage.rename(source, destination);
    if (source === state.activePath) {
        state.activePath = destination;
        ui.activePath.textContent = destination;
        ui.languageLabel.textContent = languageForPath(destination);
    }
    await refreshFiles();
    return { source, destination, renamed: true };
}

async function removeProjectFile(path) {
    assertMutablePath(path);
    await storage.remove(path);
    const wasActive = path === state.activePath;
    await refreshFiles();
    if (wasActive) {
        const next = state.files.find((entry) => !entry.readonly)?.path ?? "project.yaml";
        state.activePath = "";
        await openFile(next);
    }
    return { path, removed: true };
}

function languageForPath(path) {
    if (path.endsWith(".luau")) return "Luau";
    if (path.endsWith(".yaml") || path.endsWith(".yml")) return "YAML";
    if (path.endsWith(".json")) return "JSON";
    return "Text";
}

function showFileOperation(mode) {
    state.fileOperation = mode;
    ui.fileOperation.hidden = false;
    ui.fileOperationError.textContent = "";
    ui.fileOperationPath.readOnly = mode === "delete";
    if (mode === "new") {
        ui.fileOperationLabel.textContent = "New text file";
        ui.fileOperationPath.value = "assets/new.luau";
    } else if (mode === "rename") {
        ui.fileOperationLabel.textContent = "Rename file";
        ui.fileOperationPath.value = state.activePath;
    } else {
        ui.fileOperationLabel.textContent = "Delete file";
        ui.fileOperationPath.value = state.activePath;
    }
    ui.fileOperationPath.focus();
    ui.fileOperationPath.select();
}

function closeFileOperation() {
    state.fileOperation = null;
    ui.fileOperation.hidden = true;
    ui.fileOperationError.textContent = "";
}

async function applyFileOperation() {
    const mode = state.fileOperation;
    const path = ui.fileOperationPath.value.trim();
    if (mode === "new") {
        await createProjectFile(path, newFileContent(path), true);
        appendConsole("info", "project", `created ${path}`);
    } else if (mode === "rename") {
        const source = state.activePath;
        await renameProjectFile(source, path);
        appendConsole("info", "project", `renamed ${source} to ${path}`);
    } else if (mode === "delete") {
        await removeProjectFile(state.activePath);
        appendConsole("info", "project", `deleted ${path}`);
    }
    closeFileOperation();
}

async function saveActiveFile() {
    await storage.write(state.activePath, ui.editor.value);
    state.savedContent = ui.editor.value;
    updateDirtyState();
    appendConsole("info", "editor", `saved ${state.activePath}`);
}

async function projectSnapshot() {
    await saveActiveFile();
    const files = [];
    for (const entry of await storage.list()) {
        files.push({
            path: entry.path,
            content: entry.kind === "binary" ? await storage.bytes(entry.path) : await storage.read(entry.path),
        });
    }
    return files;
}

function stopRuntime(reason = "runtime stopped") {
    clearInterval(state.statusTimer);
    clearTimeout(state.startTimeout);
    state.statusTimer = null;
    state.startTimeout = null;
    state.messageController?.abort();
    state.messageController = null;
    state.iframe?.remove();
    state.iframe = null;
    state.channelId = null;
    ui.runtimePlaceholder.hidden = false;
    ui.inspectorScript.textContent = "—";
    ui.inspectorFrame.textContent = "—";
    setRuntimeState("stopped", "stopped");
    appendConsole("info", "runtime", reason);
}

async function playRuntime() {
    if (state.runtime === "starting" || state.runtime === "running") return;
    const files = await projectSnapshot();
    if (state.iframe) stopRuntime("replacing failed runtime");
    setRuntimeState("starting", "starting");
    appendConsole("info", "runtime", "creating isolated runtime");

    const iframe = document.createElement("iframe");
    const channelId = requestId();
    iframe.className = "runtime-frame";
    iframe.title = "fei project runtime";
    iframe.allow = "fullscreen";
    iframe.src = `../sample-browser-project.html?fei-editor-channel=${encodeURIComponent(channelId)}&dev=${Date.now()}`;
    state.iframe = iframe;
    state.channelId = channelId;
    ui.runtimePlaceholder.hidden = true;
    ui.runtimeStage.append(iframe);

    const onMessage = (event) => {
        if (
            event.source !== iframe.contentWindow ||
            event.origin !== location.origin ||
            event.data?.source !== "fei-runtime" ||
            event.data?.channelId !== channelId
        ) {
            return;
        }
        if (event.data.type === "project.request") {
            iframe.contentWindow.postMessage(
                {
                    source: "fei-editor",
                    channelId,
                    type: "project.files",
                    files,
                },
                location.origin,
            );
        } else if (event.data.type === "project.applied") {
            appendConsole("info", "runtime", "project files applied");
        } else if (event.data.type === "runtime.log") {
            if (event.data.level === "error") {
                console.error("[fei editor runtime]", event.data.message);
            }
            appendConsole(
                event.data.level === "error" ? "error" : "info",
                "game",
                event.data.message,
            );
        } else if (event.data.type === "runtime.error") {
            console.error("[fei editor runtime]", event.data.message);
            appendConsole("error", "runtime", event.data.message);
            setRuntimeState("failed", "failed");
        }
    };
    state.messageController?.abort();
    state.messageController = new AbortController();
    window.addEventListener("message", onMessage, {
        signal: state.messageController.signal,
    });

    state.statusTimer = setInterval(() => {
        if (!state.iframe || state.channelId !== channelId) return;
        try {
            const data = iframe.contentDocument?.documentElement.dataset;
            if (!data) return;
            ui.inspectorScript.textContent = data.feiProjectScript ?? "—";
            ui.inspectorFrame.textContent =
                data.feiProjectFramePresented === "true" ? "presented" : "—";
            if (data.feiProjectStatus === "web project presented") {
                clearTimeout(state.startTimeout);
                setRuntimeState("running", "running");
                document.documentElement.dataset.feiEditorProjectStatus =
                    data.feiProjectStatus;
            } else if (data.feiProjectStatus?.includes("failed")) {
                setRuntimeState("failed", data.feiProjectStatus);
            }
        } catch (error) {
            appendConsole("error", "editor", error.message);
        }
    }, 100);

    state.startTimeout = setTimeout(() => {
        if (state.runtime === "starting") {
            setRuntimeState("failed", "startup timed out");
            appendConsole("error", "runtime", "startup timed out");
        }
    }, 60000);
}

async function restartRuntime() {
    stopRuntime("restarting runtime");
    await playRuntime();
}

const commandHandlers = {
    "project.list": async () => {
        storage.assertOpen();
        return { files: state.files.map((entry) => ({ ...entry })) };
    },
    "project.read": async ({ path }) => {
        storage.validatePath(path);
        const entry = state.files.find((candidate) => candidate.path === path);
        if (!entry) throw new Error(`Unknown project file: ${path}`);
        if (entry.readonly) throw new Error(`Binary project file cannot be read as text: ${path}`);
        if (path === state.activePath) return { path, content: ui.editor.value };
        return { path, content: await storage.read(path) };
    },
    "project.write": async ({ path, content }) => {
        storage.validatePath(path);
        if (typeof content !== "string") throw new Error("project.write requires string content");
        const created = !(await storage.exists(path));
        await storage.write(path, content);
        if (path === state.activePath) {
            state.savedContent = content;
            ui.editor.value = content;
            updateLineNumbers();
            updateDirtyState();
        }
        await refreshFiles();
        return { path, saved: true, created };
    },
    "project.create": async ({ path, content }) =>
        createProjectFile(path, content ?? newFileContent(path)),
    "project.rename": async ({ path, destination }) =>
        renameProjectFile(path, destination),
    "project.remove": async ({ path }) => removeProjectFile(path),
    "runtime.play": async () => {
        await playRuntime();
        return { state: state.runtime };
    },
    "runtime.stop": async () => {
        stopRuntime("stopped by command");
        return { state: state.runtime };
    },
    "runtime.restart": async () => {
        await restartRuntime();
        return { state: state.runtime };
    },
    "runtime.status": async () => ({
        state: state.runtime,
        script: ui.inspectorScript.textContent,
        frame: ui.inspectorFrame.textContent,
    }),
};

const agentApi = Object.freeze({
    capabilities: Object.freeze(Object.keys(commandHandlers)),
    async invoke(request) {
        const id = request?.requestId ?? requestId();
        const type = request?.type;
        appendConsole("command", "agent", type ?? "invalid command");
        try {
            const handler = commandHandlers[type];
            if (!handler) throw new Error(`Unsupported editor command: ${type}`);
            return { requestId: id, ok: true, value: await handler(request) };
        } catch (error) {
            appendConsole("error", "agent", error.message);
            return {
                requestId: id,
                ok: false,
                error: { code: "command_failed", message: error.message },
            };
        }
    },
});

async function initialize() {
    const remembered = await storage.initialize();
    await refreshFiles();
    updateLineNumbers();
    updateCursor();
    updateDirtyState();
    for (const capability of agentApi.capabilities) {
        const label = document.createElement("span");
        label.className = "capability";
        label.textContent = capability;
        ui.agentCapabilities.append(label);
    }
    window.feiEditorAgent = agentApi;
    document.documentElement.dataset.feiEditorReady = "true";
    if (remembered?.restored) {
        await loadOpenedProject(remembered.name);
        appendConsole("info", "project", `restored local folder ${remembered.name}`);
    } else if (remembered?.permissionRequired) {
        ui.projectName.textContent = remembered.name;
        ui.openFolder.textContent = `Reopen ${remembered.name}`;
        ui.storageKind.textContent = "Permission required";
        appendConsole("info", "editor", `ready; reopen ${remembered.name} to grant access`);
    } else {
        appendConsole("info", "editor", "ready; open a local project folder");
    }
}

async function loadOpenedProject(name) {
    ui.projectName.textContent = name;
    ui.storageKind.textContent = name;
    ui.openFolder.textContent = "Open Folder";
    await refreshFiles();
    const preferred = state.files.find((entry) => entry.path === "assets/main.luau") ??
        state.files.find((entry) => !entry.readonly);
    state.activePath = "";
    if (preferred) await openFile(preferred.path);
}

async function openProjectFolder() {
    if (storage.root && ui.editor.value !== state.savedContent) await saveActiveFile();
    const name = await storage.open();
    stopRuntime("project folder changed");
    await loadOpenedProject(name);
    appendConsole("info", "project", `opened local folder ${name}`);
}

async function refreshProjectFolder() {
    if (ui.editor.value !== state.savedContent) await saveActiveFile();
    const activePath = state.activePath;
    await refreshFiles();
    const active = state.files.find((entry) => entry.path === activePath && !entry.readonly) ??
        state.files.find((entry) => !entry.readonly);
    state.activePath = "";
    if (active) await openFile(active.path);
    appendConsole("info", "project", "refreshed local folder");
}

ui.editor.addEventListener("input", () => {
    updateLineNumbers();
    updateCursor();
    updateDirtyState();
});
ui.openFolder.addEventListener("click", async () => {
    try {
        await openProjectFolder();
    } catch (error) {
        if (error?.name !== "AbortError") appendConsole("error", "project", error.message);
    }
});
ui.editor.addEventListener("scroll", () => {
    ui.lineNumbers.scrollTop = ui.editor.scrollTop;
});
ui.editor.addEventListener("click", updateCursor);
ui.editor.addEventListener("keyup", updateCursor);
ui.editor.addEventListener("keydown", (event) => {
    if (event.key === "Tab") {
        event.preventDefault();
        const start = ui.editor.selectionStart;
        ui.editor.setRangeText("    ", start, ui.editor.selectionEnd, "end");
        ui.editor.dispatchEvent(new Event("input"));
    }
});
window.addEventListener("keydown", (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
        event.preventDefault();
        saveActiveFile();
    }
});
ui.save.addEventListener("click", saveActiveFile);
ui.play.addEventListener("click", playRuntime);
ui.stop.addEventListener("click", () => stopRuntime());
ui.restart.addEventListener("click", restartRuntime);
ui.newFile.addEventListener("click", () => showFileOperation("new"));
ui.renameFile.addEventListener("click", () => showFileOperation("rename"));
ui.deleteFile.addEventListener("click", () => showFileOperation("delete"));
ui.cancelFileOperation.addEventListener("click", closeFileOperation);
ui.fileOperation.addEventListener("submit", async (event) => {
    event.preventDefault();
    try {
        await applyFileOperation();
    } catch (error) {
        ui.fileOperationError.textContent = error.message;
    }
});
ui.clearConsole.addEventListener("click", () => ui.consoleOutput.replaceChildren());
ui.refreshProject.addEventListener("click", async () => {
    try {
        await refreshProjectFolder();
    } catch (error) {
        appendConsole("error", "project", error.message);
    }
});
window.addEventListener("beforeunload", () => stopRuntime("editor closed"));

initialize().catch((error) => {
    document.documentElement.dataset.feiEditorReady = "failed";
    appendConsole("error", "editor", error?.stack ?? error);
});
