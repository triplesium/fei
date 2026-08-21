import { Dialog, Tooltip } from "radix-ui";
import {
    CircleStop,
    Code2,
    File,
    FileCode2,
    FileJson,
    Folder,
    FolderOpen,
    Image,
    Pencil,
    Play,
    Plus,
    RefreshCw,
    RotateCcw,
    Save,
    Trash2,
    X,
} from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Group, Panel, Separator, useDefaultLayout } from "react-resizable-panels";
import { CodeEditor } from "./components/code-editor";
import { PanelHeader, ToolPanel } from "./components/panel";
import { ProjectStorage } from "./services/project-storage";
import type {
    AgentRequest,
    AgentResponse,
    ConsoleEntry,
    ConsoleLevel,
    EditorAgentApi,
    ProjectFileEntry,
    RuntimeSession,
    RuntimeState,
} from "./types";

const storage = new ProjectStorage();
let nextLogId = 1;

function requestId(): string {
    return globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function languageForPath(path: string): string {
    if (path.endsWith(".luau") || path.endsWith(".lua")) return "Luau";
    if (path.endsWith(".yaml") || path.endsWith(".yml")) return "YAML";
    if (path.endsWith(".json")) return "JSON";
    if (/\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)$/.test(path)) return "C++";
    return "Text";
}

function newFileContent(path: string): string {
    if (path.endsWith(".luau")) {
        const moduleName = path.replace(/^assets\//, "").replace(/\.luau$/, "").replaceAll("/", ".");
        return `return module {\n    name = "${moduleName}",\n}\n`;
    }
    if (path.endsWith(".json")) return "{}\n";
    return "";
}

function fileIcon(entry: ProjectFileEntry) {
    const props = { size: 14, strokeWidth: 1.7 };
    if (entry.kind === "binary") return <Image {...props} />;
    if (entry.path.endsWith(".luau") || entry.path.endsWith(".lua")) {
        return <FileCode2 {...props} />;
    }
    if (entry.path.endsWith(".json")) return <FileJson {...props} />;
    if (entry.path.endsWith(".yaml") || entry.path.endsWith(".yml")) {
        return <Code2 {...props} />;
    }
    return <File {...props} />;
}

interface FileTreeNode {
    name: string;
    path: string;
    entry?: ProjectFileEntry;
    children: FileTreeNode[];
}

function buildFileTree(files: ProjectFileEntry[]): FileTreeNode[] {
    const root: FileTreeNode = { name: "", path: "", children: [] };
    for (const entry of files) {
        let parent = root;
        const parts = entry.path.split("/");
        parts.forEach((name, index) => {
            const path = parts.slice(0, index + 1).join("/");
            let node = parent.children.find((candidate) => candidate.name === name);
            if (!node) {
                node = { name, path, children: [] };
                parent.children.push(node);
            }
            if (index === parts.length - 1) node.entry = entry;
            parent = node;
        });
    }
    const sort = (nodes: FileTreeNode[]) => {
        nodes.sort((left, right) => {
            if (left.path === "project.yaml") return -1;
            if (right.path === "project.yaml") return 1;
            if (Boolean(left.entry) !== Boolean(right.entry)) return left.entry ? 1 : -1;
            return left.name.localeCompare(right.name);
        });
        nodes.forEach((node) => sort(node.children));
    };
    sort(root.children);
    return root.children;
}

function FileTree({
    files,
    activePath,
    onSelect,
}: {
    files: ProjectFileEntry[];
    activePath: string;
    onSelect(path: string): void;
}) {
    const renderNodes = (nodes: FileTreeNode[], depth = 0): React.ReactNode =>
        nodes.map((node) => {
            if (!node.entry) {
                return (
                    <div key={node.path} className="tree-directory">
                        <div className="directory-row" style={{ paddingLeft: 8 + depth * 14 }}>
                            <FolderOpen size={14} strokeWidth={1.6} />
                            <span>{node.name}</span>
                        </div>
                        {renderNodes(node.children, depth + 1)}
                    </div>
                );
            }
            return (
                <button
                    key={node.path}
                    className={`file-entry ${node.path === activePath ? "active" : ""}`}
                    type="button"
                    disabled={node.entry.readonly}
                    style={{ paddingLeft: 10 + depth * 14 }}
                    onClick={() => onSelect(node.path)}
                >
                    {fileIcon(node.entry)}
                    <span>{node.name}</span>
                    {node.entry.readonly && <span className="binary-tag">BIN</span>}
                </button>
            );
        });
    return <>{renderNodes(buildFileTree(files))}</>;
}

function IconButton({
    label,
    disabled,
    danger = false,
    onClick,
    children,
}: {
    label: string;
    disabled?: boolean;
    danger?: boolean;
    onClick?(): void;
    children: React.ReactNode;
}) {
    return (
        <Tooltip.Root>
            <Tooltip.Trigger asChild>
                <button
                    className={`icon-button ${danger ? "danger" : ""}`}
                    type="button"
                    aria-label={label}
                    disabled={disabled}
                    onClick={onClick}
                >
                    {children}
                </button>
            </Tooltip.Trigger>
            <Tooltip.Portal>
                <Tooltip.Content className="tooltip" sideOffset={7}>
                    {label}
                    <Tooltip.Arrow className="tooltip-arrow" />
                </Tooltip.Content>
            </Tooltip.Portal>
        </Tooltip.Root>
    );
}

function ResizeHandle({ orientation }: { orientation: "horizontal" | "vertical" }) {
    return <Separator className={`resize-handle ${orientation}`} />;
}

type Operation = "new" | "rename" | "delete" | null;
type CommandHandler = (request: AgentRequest) => Promise<unknown>;

export function App() {
    const [projectName, setProjectName] = useState("No project open");
    const [storageLabel, setStorageLabel] = useState("Local folder");
    const [openFolderLabel, setOpenFolderLabel] = useState("Open Folder");
    const [files, setFiles] = useState<ProjectFileEntry[]>([]);
    const [activePath, setActivePath] = useState("");
    const [content, setContent] = useState("");
    const [savedContent, setSavedContent] = useState("");
    const [cursor, setCursor] = useState({ line: 1, column: 1 });
    const [logs, setLogs] = useState<ConsoleEntry[]>([]);
    const [operation, setOperation] = useState<Operation>(null);
    const [operationPath, setOperationPath] = useState("");
    const [operationError, setOperationError] = useState("");
    const [runtimeState, setRuntimeState] = useState<RuntimeState>("stopped");
    const [runtimeDetail, setRuntimeDetail] = useState("stopped");
    const [runtimeScript, setRuntimeScript] = useState("—");
    const [runtimeFrame, setRuntimeFrame] = useState("—");
    const [runtimeSession, setRuntimeSession] = useState<RuntimeSession | null>(null);
    const iframeRef = useRef<HTMLIFrameElement>(null);
    const consoleRef = useRef<HTMLDivElement>(null);
    const handlersRef = useRef<Record<string, CommandHandler>>({});
    const workbenchLayout = useDefaultLayout({
        id: "fei-editor-workbench-columns-v1",
        panelIds: ["project", "main"],
        storage: localStorage,
        onlySaveAfterUserInteractions: true,
    });
    const mainLayout = useDefaultLayout({
        id: "fei-editor-main-columns-v1",
        panelIds: ["code-column", "game-column"],
        storage: localStorage,
        onlySaveAfterUserInteractions: true,
    });
    const codeLayout = useDefaultLayout({
        id: "fei-editor-code-rows-v1",
        panelIds: ["code", "console"],
        storage: localStorage,
        onlySaveAfterUserInteractions: true,
    });

    const dirty = storage.isOpen && activePath.length > 0 && content !== savedContent;

    const appendConsole = useCallback(
        (level: ConsoleLevel, source: string, message: unknown) => {
            const entry: ConsoleEntry = {
                id: nextLogId++,
                level,
                source,
                message: String(message),
                time: new Date().toLocaleTimeString([], {
                    hour12: false,
                    hour: "2-digit",
                    minute: "2-digit",
                    second: "2-digit",
                }),
            };
            setLogs((current) => [...current.slice(-499), entry]);
        },
        [],
    );

    const refreshFiles = async (): Promise<ProjectFileEntry[]> => {
        if (!storage.isOpen) {
            setFiles([]);
            return [];
        }
        const entries = await storage.list();
        setFiles(entries);
        return entries;
    };

    const saveActiveFile = async (): Promise<void> => {
        if (!activePath) return;
        await storage.write(activePath, content);
        setSavedContent(content);
        appendConsole("info", "editor", `saved ${activePath}`);
    };

    const selectFile = async (path: string): Promise<void> => {
        if (path === activePath) return;
        if (dirty) await saveActiveFile();
        const next = (await storage.read(path)) ?? "";
        setActivePath(path);
        setContent(next);
        setSavedContent(next);
        setCursor({ line: 1, column: 1 });
    };

    const loadOpenedProject = async (name: string): Promise<void> => {
        setProjectName(name);
        setStorageLabel(name);
        setOpenFolderLabel("Open Folder");
        const entries = await refreshFiles();
        const preferred =
            entries.find((entry) => entry.path === "assets/main.luau") ??
            entries.find((entry) => !entry.readonly);
        if (!preferred) return;
        const next = (await storage.read(preferred.path)) ?? "";
        setActivePath(preferred.path);
        setContent(next);
        setSavedContent(next);
    };

    const stopRuntime = (reason = "runtime stopped", log = true): void => {
        setRuntimeSession(null);
        setRuntimeState("stopped");
        setRuntimeDetail("stopped");
        setRuntimeScript("—");
        setRuntimeFrame("—");
        if (log) appendConsole("info", "runtime", reason);
    };

    const openProjectFolder = async (): Promise<void> => {
        if (dirty) await saveActiveFile();
        const name = await storage.open();
        stopRuntime("project folder changed");
        await loadOpenedProject(name);
        appendConsole("info", "project", `opened local folder ${name}`);
    };

    const refreshProjectFolder = async (): Promise<void> => {
        if (dirty) await saveActiveFile();
        const previous = activePath;
        const entries = await refreshFiles();
        const next =
            entries.find((entry) => entry.path === previous && !entry.readonly) ??
            entries.find((entry) => !entry.readonly);
        if (next) {
            const nextContent = (await storage.read(next.path)) ?? "";
            setActivePath(next.path);
            setContent(nextContent);
            setSavedContent(nextContent);
        }
        appendConsole("info", "project", "refreshed local folder");
    };

    const assertMutablePath = (path: string): void => {
        storage.validatePath(path);
        if (path === "project.yaml") {
            throw new Error("project.yaml cannot be renamed or deleted.");
        }
    };

    const createProjectFile = async (
        path: string,
        initialContent = newFileContent(path),
        select = false,
    ) => {
        storage.validatePath(path);
        if (await storage.exists(path)) throw new Error(`Project file already exists: ${path}`);
        await storage.write(path, initialContent);
        await refreshFiles();
        if (select) {
            setActivePath(path);
            setContent(initialContent);
            setSavedContent(initialContent);
        }
        return { path, created: true };
    };

    const renameProjectFile = async (source: string, destination: string) => {
        assertMutablePath(source);
        storage.validatePath(destination);
        if (source === activePath && dirty) await saveActiveFile();
        await storage.rename(source, destination);
        if (source === activePath) setActivePath(destination);
        await refreshFiles();
        return { source, destination, renamed: true };
    };

    const removeProjectFile = async (path: string) => {
        assertMutablePath(path);
        await storage.remove(path);
        const wasActive = path === activePath;
        const entries = await refreshFiles();
        if (wasActive) {
            const next = entries.find((entry) => !entry.readonly);
            if (next) {
                const nextContent = (await storage.read(next.path)) ?? "";
                setActivePath(next.path);
                setContent(nextContent);
                setSavedContent(nextContent);
            } else {
                setActivePath("");
                setContent("");
                setSavedContent("");
            }
        }
        return { path, removed: true };
    };

    const projectSnapshot = async () => {
        if (dirty) await saveActiveFile();
        const entries = await storage.list();
        return Promise.all(
            entries.map(async (entry) => ({
                path: entry.path,
                content:
                    entry.kind === "binary"
                        ? await storage.bytes(entry.path)
                        : ((await storage.read(entry.path)) ?? ""),
            })),
        );
    };

    const playRuntime = async (force = false): Promise<void> => {
        if (!force && (runtimeState === "starting" || runtimeState === "running")) return;
        const snapshot = await projectSnapshot();
        const channelId = requestId();
        setRuntimeState("starting");
        setRuntimeDetail("starting");
        setRuntimeSession({
            channelId,
            files: snapshot,
            source: `../sample-browser-project.html?fei-editor-channel=${encodeURIComponent(channelId)}&dev=${Date.now()}`,
        });
        appendConsole("info", "runtime", "creating isolated runtime");
    };

    const restartRuntime = async (): Promise<void> => {
        stopRuntime("restarting runtime");
        await playRuntime(true);
    };

    const showOperation = (next: Exclude<Operation, null>): void => {
        setOperation(next);
        setOperationError("");
        if (next === "new") setOperationPath("assets/new.luau");
        else setOperationPath(activePath);
    };

    const applyOperation = async (): Promise<void> => {
        try {
            const path = operationPath.trim();
            if (operation === "new") {
                await createProjectFile(path, newFileContent(path), true);
                appendConsole("info", "project", `created ${path}`);
            } else if (operation === "rename") {
                const source = activePath;
                await renameProjectFile(source, path);
                appendConsole("info", "project", `renamed ${source} to ${path}`);
            } else if (operation === "delete") {
                await removeProjectFile(activePath);
                appendConsole("info", "project", `deleted ${path}`);
            }
            setOperation(null);
        } catch (error) {
            setOperationError(errorMessage(error));
        }
    };

    useEffect(() => {
        let cancelled = false;
        void storage
            .initialize()
            .then(async (remembered) => {
                if (cancelled) return;
                if (remembered?.restored) {
                    await loadOpenedProject(remembered.name);
                    appendConsole("info", "project", `restored local folder ${remembered.name}`);
                } else if (remembered?.permissionRequired) {
                    setProjectName(remembered.name);
                    setStorageLabel("Permission required");
                    setOpenFolderLabel(`Reopen ${remembered.name}`);
                    appendConsole("info", "editor", `reopen ${remembered.name} to grant access`);
                } else {
                    appendConsole("info", "editor", "ready; open a local project folder");
                }
                document.documentElement.dataset.feiEditorReady = "true";
            })
            .catch((error) => {
                document.documentElement.dataset.feiEditorReady = "failed";
                appendConsole("error", "editor", errorMessage(error));
            });
        return () => {
            cancelled = true;
        };
        // Initialization intentionally runs once for the process-wide storage service.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    useEffect(() => {
        document.documentElement.dataset.feiEditorRuntime = runtimeState;
    }, [runtimeState]);

    useEffect(() => {
        consoleRef.current?.scrollTo({ top: consoleRef.current.scrollHeight });
    }, [logs]);

    useEffect(() => {
        if (!runtimeSession) return;
        const { channelId, files } = runtimeSession;
        const onMessage = (event: MessageEvent) => {
            if (
                event.source !== iframeRef.current?.contentWindow ||
                event.origin !== location.origin ||
                event.data?.source !== "fei-runtime" ||
                event.data?.channelId !== channelId
            ) {
                return;
            }
            if (event.data.type === "project.request") {
                iframeRef.current?.contentWindow?.postMessage(
                    { source: "fei-editor", channelId, type: "project.files", files },
                    location.origin,
                );
            } else if (event.data.type === "project.applied") {
                appendConsole("info", "runtime", "project files applied");
            } else if (event.data.type === "runtime.log") {
                appendConsole(event.data.level === "error" ? "error" : "info", "game", event.data.message);
            } else if (event.data.type === "runtime.error") {
                appendConsole("error", "runtime", event.data.message);
                setRuntimeState("failed");
                setRuntimeDetail("failed");
            }
        };
        window.addEventListener("message", onMessage);
        const interval = window.setInterval(() => {
            try {
                const data = iframeRef.current?.contentDocument?.documentElement.dataset;
                if (!data) return;
                setRuntimeScript(data.feiProjectScript ?? "—");
                setRuntimeFrame(data.feiProjectFramePresented === "true" ? "presented" : "—");
                if (data.feiProjectStatus === "web project presented") {
                    setRuntimeState("running");
                    setRuntimeDetail("running");
                    document.documentElement.dataset.feiEditorProjectStatus = data.feiProjectStatus;
                } else if (data.feiProjectStatus?.includes("failed")) {
                    setRuntimeState("failed");
                    setRuntimeDetail(data.feiProjectStatus);
                }
            } catch (error) {
                appendConsole("error", "editor", errorMessage(error));
            }
        }, 100);
        const timeout = window.setTimeout(() => {
            setRuntimeState((current) => {
                if (current !== "starting") return current;
                setRuntimeDetail("startup timed out");
                appendConsole("error", "runtime", "startup timed out");
                return "failed";
            });
        }, 60_000);
        return () => {
            window.removeEventListener("message", onMessage);
            window.clearInterval(interval);
            window.clearTimeout(timeout);
        };
    }, [appendConsole, runtimeSession]);

    useEffect(() => {
        const onKeyDown = (event: KeyboardEvent) => {
            if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
                event.preventDefault();
                void saveActiveFile().catch((error) =>
                    appendConsole("error", "editor", errorMessage(error)),
                );
            }
        };
        window.addEventListener("keydown", onKeyDown);
        return () => window.removeEventListener("keydown", onKeyDown);
    });

    handlersRef.current = {
        "project.list": async () => {
            storage.assertOpen();
            return { files: files.map((entry) => ({ ...entry })) };
        },
        "project.read": async ({ path }) => {
            const projectPath = storage.validatePath(path ?? "");
            const entry = files.find((candidate) => candidate.path === projectPath);
            if (!entry) throw new Error(`Unknown project file: ${projectPath}`);
            if (entry.readonly) throw new Error(`Binary project file cannot be read as text: ${projectPath}`);
            return { path: projectPath, content: projectPath === activePath ? content : await storage.read(projectPath) };
        },
        "project.write": async ({ path, content: nextContent }) => {
            const projectPath = storage.validatePath(path ?? "");
            if (typeof nextContent !== "string") throw new Error("project.write requires string content");
            const created = !(await storage.exists(projectPath));
            await storage.write(projectPath, nextContent);
            if (projectPath === activePath) {
                setContent(nextContent);
                setSavedContent(nextContent);
            }
            await refreshFiles();
            return { path: projectPath, saved: true, created };
        },
        "project.create": async ({ path, content: initial }) =>
            createProjectFile(path ?? "", initial ?? newFileContent(path ?? "")),
        "project.rename": async ({ path, destination }) =>
            renameProjectFile(path ?? "", destination ?? ""),
        "project.remove": async ({ path }) => removeProjectFile(path ?? ""),
        "runtime.play": async () => {
            await playRuntime();
            return { state: "starting" };
        },
        "runtime.stop": async () => {
            stopRuntime("stopped by command");
            return { state: "stopped" };
        },
        "runtime.restart": async () => {
            await restartRuntime();
            return { state: "starting" };
        },
        "runtime.status": async () => ({
            state: runtimeState,
            script: runtimeScript,
            frame: runtimeFrame,
        }),
    };

    const agentApi = useMemo<EditorAgentApi>(() => {
        const invoke = async (request: AgentRequest): Promise<AgentResponse> => {
            const id = request?.requestId ?? requestId();
            const type = request?.type ?? "";
            appendConsole("command", "agent", type || "invalid command");
            try {
                const handler = handlersRef.current[type];
                if (!handler) throw new Error(`Unsupported editor command: ${type}`);
                return { requestId: id, ok: true, value: await handler(request) };
            } catch (error) {
                const message = errorMessage(error);
                appendConsole("error", "agent", message);
                return { requestId: id, ok: false, error: { code: "command_failed", message } };
            }
        };
        return {
            capabilities: Object.freeze([
                "project.list",
                "project.read",
                "project.write",
                "project.create",
                "project.rename",
                "project.remove",
                "runtime.play",
                "runtime.stop",
                "runtime.restart",
                "runtime.status",
            ]),
            invoke,
        };
    }, [appendConsole]);

    useEffect(() => {
        window.feiEditorAgent = agentApi;
    }, [agentApi]);

    const protectedFile = activePath === "project.yaml";
    const canEdit = storage.isOpen && activePath.length > 0;
    const operationTitle =
        operation === "new" ? "Create file" : operation === "rename" ? "Rename file" : "Delete file";

    return (
        <Tooltip.Provider delayDuration={450}>
            <div className="app-shell">
                <header className="topbar">
                    <div className="brand">
                        <span className="brand-mark">F</span>
                        <div className="brand-copy">
                            <span>FEI EDITOR</span>
                            <strong>{projectName}</strong>
                        </div>
                    </div>
                    <div className="toolbar" aria-label="Project and runtime controls">
                        <button
                            id="open-folder"
                            className="button folder-button"
                            type="button"
                            onClick={() =>
                                void openProjectFolder().catch((error) => {
                                    if (!(error instanceof DOMException) || error.name !== "AbortError") {
                                        appendConsole("error", "project", errorMessage(error));
                                    }
                                })
                            }
                        >
                            <FolderOpen size={15} />
                            {openFolderLabel}
                        </button>
                        <IconButton label="Save (Ctrl+S)" disabled={!canEdit} onClick={() => void saveActiveFile()}>
                            <Save size={15} />
                        </IconButton>
                        <span className="toolbar-divider" />
                        <button
                            id="play"
                            className="button primary"
                            type="button"
                            disabled={!storage.isOpen || runtimeState === "starting" || runtimeState === "running"}
                            onClick={() => void playRuntime()}
                        >
                            <Play size={14} fill="currentColor" /> Play
                        </button>
                        <IconButton
                            label="Stop"
                            danger
                            disabled={runtimeState === "stopped"}
                            onClick={() => stopRuntime()}
                        >
                            <CircleStop size={15} />
                        </IconButton>
                        <IconButton
                            label="Restart"
                            disabled={runtimeState !== "running" && runtimeState !== "failed"}
                            onClick={() => void restartRuntime()}
                        >
                            <RotateCcw size={15} />
                        </IconButton>
                        <div className={`runtime-badge ${runtimeState}`}>
                            <span className="status-dot" />
                            {runtimeState}
                        </div>
                    </div>
                </header>

                <main className="workbench">
                    <Group
                        orientation="horizontal"
                        id="workbench-columns"
                        defaultLayout={workbenchLayout.defaultLayout}
                        onLayoutChanged={workbenchLayout.onLayoutChanged}
                    >
                        <Panel id="project" defaultSize={235} minSize={180} maxSize={360}>
                            <ToolPanel className="project-panel">
                                <PanelHeader
                                    title="PROJECT"
                                    detail={files.length > 0 ? String(files.length) : undefined}
                                    actions={
                                        <>
                                            <IconButton label="New file" disabled={!storage.isOpen} onClick={() => showOperation("new")}>
                                                <Plus size={14} />
                                            </IconButton>
                                            <IconButton label="Rename" disabled={!canEdit || protectedFile} onClick={() => showOperation("rename")}>
                                                <Pencil size={13} />
                                            </IconButton>
                                            <IconButton label="Delete" danger disabled={!canEdit || protectedFile} onClick={() => showOperation("delete")}>
                                                <Trash2 size={14} />
                                            </IconButton>
                                            <IconButton label="Refresh folder" disabled={!storage.isOpen} onClick={() => void refreshProjectFolder()}>
                                                <RefreshCw size={14} />
                                            </IconButton>
                                        </>
                                    }
                                />
                                <nav className="file-tree" aria-label="Project files">
                                    {!storage.isOpen ? (
                                        <div className="empty-tree">
                                            <Folder size={28} strokeWidth={1.3} />
                                            <span>Open a local project folder to begin.</span>
                                        </div>
                                    ) : (
                                        <FileTree
                                            files={files}
                                            activePath={activePath}
                                            onSelect={(path) => void selectFile(path)}
                                        />
                                    )}
                                </nav>
                                <footer className="panel-status">
                                    <span title={storageLabel}>{storageLabel}</span>
                                    <span className={dirty ? "dirty" : ""}>{canEdit ? (dirty ? "Unsaved" : "Saved") : "—"}</span>
                                </footer>
                            </ToolPanel>
                        </Panel>
                        <ResizeHandle orientation="horizontal" />
                        <Panel id="main" minSize={480}>
                            <Group
                                orientation="horizontal"
                                id="main-columns"
                                defaultLayout={mainLayout.defaultLayout}
                                onLayoutChanged={mainLayout.onLayoutChanged}
                            >
                                <Panel id="code-column" minSize={400}>
                                    <Group
                                        orientation="vertical"
                                        id="code-rows"
                                        defaultLayout={codeLayout.defaultLayout}
                                        onLayoutChanged={codeLayout.onLayoutChanged}
                                    >
                                        <Panel id="code" minSize={240}>
                                            <ToolPanel className="editor-panel">
                                                <PanelHeader
                                                    title={activePath || "EDITOR"}
                                                    detail={dirty ? "●" : languageForPath(activePath)}
                                                />
                                                <CodeEditor
                                                    path={activePath}
                                                    value={content}
                                                    readOnly={!canEdit}
                                                    onChange={setContent}
                                                    onCursorChange={(line, column) => setCursor({ line, column })}
                                                />
                                                <footer className="editor-status">
                                                    <span>Ln {cursor.line}, Col {cursor.column}</span>
                                                    <span>{languageForPath(activePath)}</span>
                                                    <span>UTF-8</span>
                                                </footer>
                                            </ToolPanel>
                                        </Panel>
                                        <ResizeHandle orientation="vertical" />
                                        <Panel id="console" defaultSize={190} minSize={90} maxSize="42%" collapsible>
                                            <ToolPanel className="console-panel">
                                                <PanelHeader
                                                    title="CONSOLE"
                                                    detail={`${logs.length}`}
                                                    actions={<button className="text-button" type="button" onClick={() => setLogs([])}>Clear</button>}
                                                />
                                                <div ref={consoleRef} className="console-output" aria-live="polite">
                                                    {logs.map((entry) => (
                                                        <div key={entry.id} className={`console-line ${entry.level}`}>
                                                            <span>{entry.time}</span>
                                                            <span>{entry.source}</span>
                                                            <span>{entry.message}</span>
                                                        </div>
                                                    ))}
                                                </div>
                                            </ToolPanel>
                                        </Panel>
                                    </Group>
                                </Panel>
                                <ResizeHandle orientation="horizontal" />
                                <Panel
                                    id="game-column"
                                    defaultSize={540}
                                    minSize={380}
                                    maxSize={760}
                                    groupResizeBehavior="preserve-pixel-size"
                                >
                                    <aside className="game-column">
                                        <ToolPanel className="preview-panel">
                                            <PanelHeader title="GAME" detail={`16:9 · ${runtimeDetail}`} />
                                            <div className="runtime-stage">
                                                {runtimeSession ? (
                                                    <iframe
                                                        ref={iframeRef}
                                                        className="runtime-frame"
                                                        title="fei project runtime"
                                                        allow="fullscreen"
                                                        src={runtimeSession.source}
                                                    />
                                                ) : (
                                                    <div className="runtime-placeholder">
                                                        <span className="placeholder-play"><Play size={19} fill="currentColor" /></span>
                                                        <strong>Project is stopped</strong>
                                                        <span>Press Play to start the WebAssembly runtime.</span>
                                                    </div>
                                                )}
                                            </div>
                                        </ToolPanel>
                                        <ToolPanel className="inspector-panel">
                                            <PanelHeader title="INSPECTOR" />
                                            <div className="inspector-section">
                                                <span className="section-label">RUNTIME</span>
                                                <dl className="property-list">
                                                    <div><dt>State</dt><dd>{runtimeDetail}</dd></div>
                                                    <div><dt>Script</dt><dd>{runtimeScript}</dd></div>
                                                    <div><dt>Frame</dt><dd>{runtimeFrame}</dd></div>
                                                </dl>
                                            </div>
                                            <div className="inspector-section">
                                                <span className="section-label">AGENT API</span>
                                                <p className="muted-copy">UI and agents use the same command bus through <code>window.feiEditorAgent</code>.</p>
                                                <div className="capabilities">
                                                    {agentApi.capabilities.map((capability) => <code key={capability}>{capability}</code>)}
                                                </div>
                                            </div>
                                        </ToolPanel>
                                    </aside>
                                </Panel>
                            </Group>
                        </Panel>
                    </Group>
                </main>

                <Dialog.Root open={operation !== null} onOpenChange={(open) => !open && setOperation(null)}>
                    <Dialog.Portal>
                        <Dialog.Overlay className="dialog-overlay" />
                        <Dialog.Content className="dialog-content">
                            <div className="dialog-heading">
                                <Dialog.Title>{operationTitle}</Dialog.Title>
                                <Dialog.Close asChild>
                                    <button className="icon-button" type="button" aria-label="Close"><X size={15} /></button>
                                </Dialog.Close>
                            </div>
                            <Dialog.Description>
                                {operation === "delete"
                                    ? "This removes the file from the local project folder."
                                    : "Paths are relative to the project root and must be inside assets/."}
                            </Dialog.Description>
                            <label className="field-label" htmlFor="operation-path">Project path</label>
                            <input
                                id="operation-path"
                                className="text-field"
                                value={operationPath}
                                readOnly={operation === "delete"}
                                autoFocus
                                spellCheck={false}
                                onChange={(event) => setOperationPath(event.target.value)}
                                onKeyDown={(event) => {
                                    if (event.key === "Enter") void applyOperation();
                                }}
                            />
                            {operationError && <p className="dialog-error">{operationError}</p>}
                            <div className="dialog-actions">
                                <Dialog.Close asChild><button className="button" type="button">Cancel</button></Dialog.Close>
                                <button className={`button ${operation === "delete" ? "danger" : "primary"}`} type="button" onClick={() => void applyOperation()}>
                                    {operation === "delete" ? "Delete" : "Apply"}
                                </button>
                            </div>
                        </Dialog.Content>
                    </Dialog.Portal>
                </Dialog.Root>
            </div>
        </Tooltip.Provider>
    );
}
