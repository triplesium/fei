import { Dialog, DropdownMenu, Tooltip } from "radix-ui";
import {
    DockviewReact,
    type DockviewApi,
    type DockviewReadyEvent,
    type IDockviewPanelHeaderProps,
    type IDockviewPanelProps,
} from "dockview-react";
import {
    CircleStop,
    Bot,
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
    Settings2,
    Terminal,
    Trash2,
    X,
} from "lucide-react";
import {
    createContext,
    useCallback,
    useContext,
    useEffect,
    useMemo,
    useRef,
    useState,
    type ReactNode,
} from "react";
import { parseDocument } from "yaml";
import { EditorPiAgent } from "./agent/editor-pi-agent";
import { EditorModelGateway, type ModelGatewayState } from "./agent/model-gateway";
import { PiAssistantThread } from "./agent/pi-assistant-thread";
import { CodeEditor } from "./components/code-editor";
import { ToolPanel } from "./components/panel";
import { WasmRuntimeController } from "./runtime/wasm-runtime-controller";
import { ProjectStorage } from "./services/project-storage";
import type {
    AgentRequest,
    AgentResponse,
    ConsoleEntry,
    ConsoleLevel,
    EditorAgentApi,
    ProjectFileEntry,
    ProjectSettings,
} from "./types";

const storage = new ProjectStorage();

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

const assetPathPrefix = "assets/";

function assetRelativePath(path: string): string {
    return path.startsWith(assetPathPrefix) ? path.slice(assetPathPrefix.length) : path;
}

function projectAssetPath(path: string): string {
    return path.startsWith(assetPathPrefix) ? path : `${assetPathPrefix}${path}`;
}

function parseProjectSettings(source: string): ProjectSettings {
    const document = parseDocument(source);
    if (document.errors.length > 0) throw new Error(document.errors[0].message);
    const value = document.toJS() as Record<string, unknown> | null;
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Project configuration must be a YAML mapping.");
    }
    const runtime = value.runtime;
    const runtimeMap =
        runtime && typeof runtime === "object" && !Array.isArray(runtime)
            ? (runtime as Record<string, unknown>)
            : {};
    const plugins = runtimeMap.plugins ?? [];
    if (!Array.isArray(plugins) || plugins.some((plugin) => typeof plugin !== "string")) {
        throw new Error("Project field 'runtime.plugins' must be a list of plugin ids.");
    }
    return {
        name: typeof value.name === "string" ? value.name : "",
        assetDirectory:
            typeof value.asset_directory === "string" ? value.asset_directory : "assets",
        runtimePlugins: plugins as string[],
    };
}

function validateProjectSettings(settings: ProjectSettings): ProjectSettings {
    if (typeof settings.name !== "string" || typeof settings.assetDirectory !== "string") {
        throw new Error("Project name and asset directory must be strings.");
    }
    if (!Array.isArray(settings.runtimePlugins) || settings.runtimePlugins.some((plugin) => typeof plugin !== "string")) {
        throw new Error("Runtime plugins must be a list of plugin ids.");
    }
    const name = settings.name.trim();
    const assetDirectory = settings.assetDirectory.trim();
    const runtimePlugins = settings.runtimePlugins.map((plugin) => plugin.trim()).filter(Boolean);
    if (!name) throw new Error("Project name is required.");
    if (
        !assetDirectory ||
        assetDirectory.startsWith("/") ||
        assetDirectory.includes("\\") ||
        assetDirectory.split("/").some((part) => !part || part === "." || part === "..")
    ) {
        throw new Error("Asset directory must be a relative path inside the project folder.");
    }
    if (new Set(runtimePlugins).size !== runtimePlugins.length) {
        throw new Error("Runtime plugins cannot contain duplicates.");
    }
    if (runtimePlugins.some((plugin) => !/^[A-Za-z_]\w*(?:::[A-Za-z_]\w*)+$/.test(plugin))) {
        throw new Error("Runtime plugin ids must use qualified names such as project_runtime::LuauScripts.");
    }
    return { name, assetDirectory, runtimePlugins };
}

function updateProjectSettingsSource(source: string, settings: ProjectSettings): string {
    const document = parseDocument(source);
    if (document.errors.length > 0) throw new Error(document.errors[0].message);
    document.set("name", settings.name);
    document.set("asset_directory", settings.assetDirectory);
    if (!document.has("runtime")) document.set("runtime", {});
    document.setIn(["runtime", "plugins"], settings.runtimePlugins);
    return document.toString({ lineWidth: 0 });
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
        const displayPath = assetRelativePath(entry.path);
        const parts = displayPath.split("/");
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
                    className={`file-entry ${node.entry.path === activePath ? "active" : ""}`}
                    type="button"
                    disabled={node.entry.readonly}
                    style={{ paddingLeft: 10 + depth * 14 }}
                    onClick={() => onSelect(node.entry!.path)}
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
    id,
    label,
    disabled,
    danger = false,
    onClick,
    children,
}: {
    id?: string;
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
                    id={id}
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

type Operation = "new" | "rename" | "delete" | null;
type CommandHandler = (request: AgentRequest) => Promise<unknown>;

const workbenchLayoutStorageKey = "fei-editor-dockview-layout-v2";
const dockviewComponents = { panel: DockPanel };
const dockviewTabComponents = { engine: EnginePanelTab };
const WorkbenchPanelsContext = createContext<Record<string, ReactNode>>({});

function DockPanel({ api }: IDockviewPanelProps) {
    const panels = useContext(WorkbenchPanelsContext);
    return panels[api.id] ?? <div className="missing-panel">Panel unavailable</div>;
}

function EnginePanelTab({ api }: IDockviewPanelHeaderProps) {
    const Icon =
        api.id === "project"
            ? Folder
            : api.id === "code"
              ? Code2
              : api.id === "game"
                ? Play
                : api.id === "agent"
                  ? Bot
                : api.id === "inspector"
                  ? Settings2
                  : Terminal;
    return (
        <div className="engine-panel-tab">
            <Icon size={13} strokeWidth={1.7} />
            <span>{api.title}</span>
        </div>
    );
}

function addDefaultWorkbenchPanels(api: DockviewApi): void {
    api.addPanel({
        id: "project",
        component: "panel",
        tabComponent: "engine",
        title: "Assets",
        initialWidth: 230,
        minimumWidth: 180,
        maximumWidth: 360,
    });
    api.addPanel({
        id: "code",
        component: "panel",
        tabComponent: "engine",
        title: "Code",
        position: { referencePanel: "project", direction: "right" },
        initialWidth: 660,
        minimumWidth: 380,
    });
    api.addPanel({
        id: "game",
        component: "panel",
        tabComponent: "engine",
        title: "Game",
        renderer: "always",
        position: { referencePanel: "code", direction: "right" },
        initialWidth: 520,
        minimumWidth: 360,
    });
    api.addPanel({
        id: "agent",
        component: "panel",
        tabComponent: "engine",
        title: "Agent",
        renderer: "always",
        position: { referencePanel: "game", direction: "right" },
        initialWidth: 360,
        minimumWidth: 280,
    });
    api.addPanel({
        id: "inspector",
        component: "panel",
        tabComponent: "engine",
        title: "Inspector",
        position: { referencePanel: "game", direction: "below" },
        initialHeight: 290,
        minimumWidth: 260,
        minimumHeight: 120,
    });
    api.addPanel({
        id: "console",
        component: "panel",
        tabComponent: "engine",
        title: "Console",
        position: { referencePanel: "code", direction: "below" },
        initialHeight: 180,
        minimumHeight: 90,
    });

    const gameWidth = Math.max(460, Math.min(760, Math.round(api.width * 0.4)));
    api.getPanel("project")?.group.api.setSize({ width: 230 });
    api.getPanel("game")?.group.api.setSize({ width: gameWidth });
    api.getPanel("console")?.group.api.setSize({ height: 180 });
    api.getPanel("inspector")?.group.api.setSize({ height: 280 });
}

export function App() {
    const runtimeControllerRef = useRef<WasmRuntimeController | null>(null);
    if (runtimeControllerRef.current === null) {
        runtimeControllerRef.current = new WasmRuntimeController();
    }
    const runtimeController = runtimeControllerRef.current;

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
    const [settingsOpen, setSettingsOpen] = useState(false);
    const [settingsDraft, setSettingsDraft] = useState<ProjectSettings>({
        name: "",
        assetDirectory: "assets",
        runtimePlugins: [],
    });
    const [settingsPlugins, setSettingsPlugins] = useState("");
    const [settingsError, setSettingsError] = useState("");
    const [agentSettingsOpen, setAgentSettingsOpen] = useState(false);
    const [agentApiKey, setAgentApiKey] = useState("");
    const [agentSettingsError, setAgentSettingsError] = useState("");
    const [agentSettingsSaving, setAgentSettingsSaving] = useState(false);
    const [agentGatewayState, setAgentGatewayState] = useState<ModelGatewayState>({
        state: "connecting",
    });
    const [agentStreaming, setAgentStreaming] = useState(false);
    const [runtimeSnapshot, setRuntimeSnapshot] = useState(() => runtimeController.getSnapshot());
    const consoleRef = useRef<HTMLDivElement>(null);
    const handlersRef = useRef<Record<string, CommandHandler>>({});
    const dockviewApiRef = useRef<DockviewApi | null>(null);
    const dockviewLayoutListenerRef = useRef<{ dispose(): void } | null>(null);

    const dirty = storage.isOpen && activePath.length > 0 && content !== savedContent;

    const appendConsole = useCallback(
        (level: ConsoleLevel, source: string, message: unknown) => {
            const entry: ConsoleEntry = {
                id: requestId(),
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

    useEffect(() => {
        const unsubscribe = runtimeController.subscribe((event) => {
            if (event.type === "snapshot") {
                setRuntimeSnapshot(event.snapshot);
            } else {
                appendConsole(event.level, event.source, event.message);
            }
        });
        return () => {
            unsubscribe();
            runtimeController.dispose();
        };
    }, [appendConsole, runtimeController]);

    const {
        state: runtimeState,
        detail: runtimeDetail,
        script: runtimeScript,
        frame: runtimeFrame,
        session: runtimeSession,
    } = runtimeSnapshot;

    const attachRuntimeFrame = useCallback(
        (frame: HTMLIFrameElement | null) => runtimeController.attachFrame(frame),
        [runtimeController],
    );

    const refreshFiles = async (): Promise<ProjectFileEntry[]> => {
        if (!storage.isOpen) {
            setFiles([]);
            return [];
        }
        const entries = await storage.list();
        const assetEntries = entries.filter((entry) => entry.path.startsWith(assetPathPrefix));
        setFiles(assetEntries);
        return assetEntries;
    };

    const readProjectSettings = async (): Promise<ProjectSettings> => {
        storage.assertOpen();
        const source = await storage.read("project.yaml");
        if (source === null) throw new Error("Project manifest project.yaml was not found.");
        return parseProjectSettings(source);
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
        setStorageLabel(name);
        setOpenFolderLabel("Open Folder");
        try {
            const settings = await readProjectSettings();
            if (!settings.name.trim()) throw new Error("Project name is required.");
            setProjectName(settings.name);
        } catch (error) {
            setProjectName(name);
            appendConsole("error", "project", `invalid project settings: ${errorMessage(error)}`);
        }
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
        runtimeController.stop(reason, log);
    };

    const writeProjectSettings = async (settings: ProjectSettings): Promise<ProjectSettings> => {
        const next = validateProjectSettings(settings);
        const source = await storage.read("project.yaml");
        if (source === null) throw new Error("Project manifest project.yaml was not found.");
        const current = parseProjectSettings(source);
        if (next.assetDirectory !== current.assetDirectory) {
            throw new Error("Changing the asset directory is not supported by this development editor yet.");
        }
        await storage.write("project.yaml", updateProjectSettingsSource(source, next));
        setProjectName(next.name);
        if (runtimeState !== "stopped") stopRuntime("project settings changed");
        return next;
    };

    const showProjectSettings = async (): Promise<void> => {
        if (dirty) await saveActiveFile();
        const settings = await readProjectSettings();
        setSettingsDraft(settings);
        setSettingsPlugins(settings.runtimePlugins.join("\n"));
        setSettingsError("");
        setSettingsOpen(true);
    };

    const applyProjectSettings = async (): Promise<void> => {
        try {
            const next = await writeProjectSettings({
                ...settingsDraft,
                runtimePlugins: settingsPlugins.split(/\r?\n/),
            });
            setSettingsDraft(next);
            setSettingsPlugins(next.runtimePlugins.join("\n"));
            setSettingsOpen(false);
            appendConsole("info", "project", "project settings saved");
        } catch (error) {
            setSettingsError(errorMessage(error));
        }
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
        if (!path.startsWith(assetPathPrefix)) {
            throw new Error("Only files inside the asset directory can be changed directly.");
        }
    };

    const createProjectFile = async (
        path: string,
        initialContent = newFileContent(path),
        select = false,
    ) => {
        assertMutablePath(path);
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
        assertMutablePath(destination);
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
        await runtimeController.start(snapshot, force);
    };

    const restartRuntime = async (): Promise<void> => {
        const snapshot = await projectSnapshot();
        await runtimeController.restart(snapshot);
    };

    const showOperation = (next: Exclude<Operation, null>): void => {
        setOperation(next);
        setOperationError("");
        if (next === "new") setOperationPath("new.luau");
        else setOperationPath(assetRelativePath(activePath));
    };

    const applyOperation = async (): Promise<void> => {
        try {
            const displayPath = operationPath.trim();
            const path = projectAssetPath(displayPath);
            if (operation === "new") {
                await createProjectFile(path, newFileContent(path), true);
                appendConsole("info", "project", `created ${displayPath}`);
            } else if (operation === "rename") {
                const source = activePath;
                await renameProjectFile(source, path);
                appendConsole("info", "project", `renamed ${assetRelativePath(source)} to ${displayPath}`);
            } else if (operation === "delete") {
                await removeProjectFile(activePath);
                appendConsole("info", "project", `deleted ${displayPath}`);
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
        let refreshTimer: ReturnType<typeof setTimeout> | undefined;
        const unsubscribe = storage.subscribe((event) => {
            if (refreshTimer) clearTimeout(refreshTimer);
            refreshTimer = setTimeout(() => {
                void refreshFiles()
                    .then(async () => {
                        if (!dirty && activePath && event.path === activePath) {
                            const nextContent = await storage.read(activePath);
                            if (nextContent !== null) {
                                setContent(nextContent);
                                setSavedContent(nextContent);
                            }
                        }
                    })
                    .catch((error) => appendConsole("error", "project", errorMessage(error)));
            }, 120);
        });
        return () => {
            if (refreshTimer) clearTimeout(refreshTimer);
            unsubscribe();
        };
    }, [activePath, appendConsole, dirty]);

    useEffect(() => () => storage.dispose(), []);

    useEffect(() => {
        consoleRef.current?.scrollTo({ top: consoleRef.current.scrollHeight });
    }, [logs]);

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
        "project.settings.get": async () => readProjectSettings(),
        "project.settings.update": async ({ settings }) => {
            if (!settings || typeof settings !== "object") {
                throw new Error("project.settings.update requires a settings object");
            }
            const current = await readProjectSettings();
            return writeProjectSettings({
                name: settings.name ?? current.name,
                assetDirectory: settings.assetDirectory ?? current.assetDirectory,
                runtimePlugins: settings.runtimePlugins ?? current.runtimePlugins,
            });
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
            assertMutablePath(projectPath);
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
                "project.settings.get",
                "project.settings.update",
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

    const piAgent = useMemo(() => new EditorPiAgent(agentApi), [agentApi]);
    const modelGateway = useMemo(() => new EditorModelGateway(), []);

    useEffect(() => {
        window.feiEditorPi = piAgent;
        return () => piAgent.dispose();
    }, [piAgent]);

    useEffect(() => {
        const updateStreaming = () => setAgentStreaming(piAgent.snapshot().streaming);
        updateStreaming();
        return piAgent.subscribeState(updateStreaming);
    }, [piAgent]);

    useEffect(() => {
        let cancelled = false;
        void modelGateway.connect(piAgent).then((state) => {
            if (!cancelled) setAgentGatewayState(state);
        });
        return () => {
            cancelled = true;
        };
    }, [modelGateway, piAgent]);

    const saveAgentApiKey = async (): Promise<void> => {
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.saveDeepSeekApiKey(agentApiKey, piAgent);
            setAgentGatewayState(state);
            setAgentApiKey("");
            setAgentSettingsOpen(false);
            appendConsole("info", "agent", "DeepSeek model gateway configured");
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const removeAgentApiKey = async (): Promise<void> => {
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.removeDeepSeekApiKey(piAgent);
            setAgentGatewayState(state);
            setAgentApiKey("");
            appendConsole("info", "agent", "DeepSeek credential removed");
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const resetAgentConversation = (): void => {
        if (agentStreaming) piAgent.abort();
        else piAgent.reset();
        setAgentStreaming(false);
    };

    useEffect(
        () => () => {
            dockviewLayoutListenerRef.current?.dispose();
        },
        [],
    );

    const onDockviewReady = useCallback((event: DockviewReadyEvent) => {
        const { api } = event;
        dockviewApiRef.current = api;
        const savedLayout = localStorage.getItem(workbenchLayoutStorageKey);
        if (savedLayout) {
            try {
                api.fromJSON(JSON.parse(savedLayout));
            } catch (error) {
                console.warn("[fei editor] discarded invalid workbench layout", error);
                localStorage.removeItem(workbenchLayoutStorageKey);
                addDefaultWorkbenchPanels(api);
                localStorage.setItem(workbenchLayoutStorageKey, JSON.stringify(api.toJSON()));
            }
        } else {
            addDefaultWorkbenchPanels(api);
            localStorage.setItem(workbenchLayoutStorageKey, JSON.stringify(api.toJSON()));
        }

        dockviewLayoutListenerRef.current?.dispose();
        api.getPanel("project")?.api.setTitle("Assets");
        dockviewLayoutListenerRef.current = api.onDidLayoutChange(() => {
            localStorage.setItem(workbenchLayoutStorageKey, JSON.stringify(api.toJSON()));
        });
    }, []);

    const resetWorkbenchLayout = useCallback(() => {
        const api = dockviewApiRef.current;
        if (!api) return;
        localStorage.removeItem(workbenchLayoutStorageKey);
        api.clear();
        addDefaultWorkbenchPanels(api);
        localStorage.setItem(workbenchLayoutStorageKey, JSON.stringify(api.toJSON()));
    }, []);

    const canEdit = storage.isOpen && activePath.length > 0;
    const operationTitle =
        operation === "new" ? "Create file" : operation === "rename" ? "Rename file" : "Delete file";

    useEffect(() => {
        dockviewApiRef.current
            ?.getPanel("code")
            ?.api.setTitle(
                activePath ? `${assetRelativePath(activePath)}${dirty ? " •" : ""}` : "Code",
            );
    }, [activePath, dirty]);

    const workbenchPanels: Record<string, ReactNode> = {
        project: (
            <ToolPanel className="project-panel">
                <div className="panel-commandbar">
                    <span>{files.length > 0 ? `${files.length} files` : storageLabel}</span>
                    <div className="panel-actions">
                        <IconButton
                            id="project-settings"
                            label="Project settings"
                            disabled={!storage.isOpen}
                            onClick={() =>
                                void showProjectSettings().catch((error) =>
                                    appendConsole("error", "project", errorMessage(error)),
                                )
                            }
                        >
                            <Settings2 size={14} />
                        </IconButton>
                        <IconButton label="New file" disabled={!storage.isOpen} onClick={() => showOperation("new")}>
                            <Plus size={14} />
                        </IconButton>
                        <IconButton label="Rename" disabled={!canEdit} onClick={() => showOperation("rename")}>
                            <Pencil size={13} />
                        </IconButton>
                        <IconButton label="Delete" danger disabled={!canEdit} onClick={() => showOperation("delete")}>
                            <Trash2 size={14} />
                        </IconButton>
                        <IconButton label="Refresh folder" disabled={!storage.isOpen} onClick={() => void refreshProjectFolder()}>
                            <RefreshCw size={14} />
                        </IconButton>
                    </div>
                </div>
                <nav className="file-tree" aria-label="Project files">
                    {!storage.isOpen ? (
                        <div className="empty-tree">
                            <Folder size={28} strokeWidth={1.3} />
                            <span>Open a local project folder to begin.</span>
                        </div>
                    ) : (
                        <FileTree files={files} activePath={activePath} onSelect={(path) => void selectFile(path)} />
                    )}
                </nav>
                <footer className="panel-status">
                    <span title={storageLabel}>{storageLabel}</span>
                    <span className={dirty ? "dirty" : ""}>{canEdit ? (dirty ? "Unsaved" : "Saved") : "—"}</span>
                </footer>
            </ToolPanel>
        ),
        code: (
            <ToolPanel className="editor-panel">
                <CodeEditor
                    path={activePath}
                    value={content}
                    readOnly={!canEdit}
                    onChange={setContent}
                    onCursorChange={(line, column) => setCursor({ line, column })}
                />
                <footer className="editor-status">
                    <span>{dirty ? "● Unsaved" : "Saved"}</span>
                    <span>Ln {cursor.line}, Col {cursor.column}</span>
                    <span>{languageForPath(activePath)}</span>
                    <span>UTF-8</span>
                </footer>
            </ToolPanel>
        ),
        console: (
            <ToolPanel className="console-panel">
                <div className="panel-commandbar">
                    <span>{logs.length} messages</span>
                    <button className="text-button" type="button" onClick={() => setLogs([])}>Clear</button>
                </div>
                <div ref={consoleRef} className="console-output" aria-live="polite">
                    {logs.length === 0 && <div className="console-empty">No console output.</div>}
                    {logs.map((entry) => (
                        <div key={entry.id} className={`console-line ${entry.level}`}>
                            <span>{entry.time}</span>
                            <span>{entry.source}</span>
                            <span>{entry.message}</span>
                        </div>
                    ))}
                </div>
            </ToolPanel>
        ),
        game: (
            <ToolPanel className="preview-panel">
                <div className="viewport-toolbar">
                    <span>16:9</span>
                </div>
                <div className="runtime-stage">
                    {runtimeSession ? (
                        <iframe
                            ref={attachRuntimeFrame}
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
        ),
        agent: (
            <ToolPanel className="agent-panel">
                <div className="panel-commandbar">
                    <span className="agent-model-label">
                        <i className={agentGatewayState.state === "ready" ? "ready" : ""} />
                        {agentGatewayState.state === "ready" ? agentGatewayState.model : "Agent"}
                    </span>
                    <div className="panel-actions">
                        {agentStreaming && (
                            <button className="text-button" type="button" onClick={() => piAgent.abort()}>
                                Stop
                            </button>
                        )}
                        <button
                            className="text-button"
                            type="button"
                            disabled={agentStreaming}
                            onClick={resetAgentConversation}
                        >
                            New chat
                        </button>
                    </div>
                </div>
                <PiAssistantThread
                    agent={piAgent}
                    enabled={agentGatewayState.state === "ready"}
                    model={agentGatewayState.state === "ready" ? agentGatewayState.model : undefined}
                    onConfigure={() => setAgentSettingsOpen(true)}
                />
            </ToolPanel>
        ),
        inspector: (
            <ToolPanel className="inspector-panel">
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
                <div className="inspector-section">
                    <span className="section-label">PI TOOLS</span>
                    <p className="muted-copy">
                        {agentGatewayState.state === "ready"
                            ? `${agentGatewayState.provider} · ${agentGatewayState.model}`
                            : agentGatewayState.state === "unconfigured"
                              ? `${agentGatewayState.provider} credential required`
                              : agentGatewayState.state === "unavailable"
                                ? "Local model gateway unavailable"
                                : "Connecting to local model gateway…"}
                    </p>
                    <div className="capabilities">
                        {piAgent.status().tools.map((tool) => <code key={tool}>{tool}</code>)}
                    </div>
                    <button
                        className="button"
                        type="button"
                        onClick={() => {
                            setAgentSettingsError("");
                            setAgentSettingsOpen(true);
                        }}
                    >
                        Configure DeepSeek
                    </button>
                </div>
            </ToolPanel>
        ),
    };

    return (
        <Tooltip.Provider delayDuration={450}>
            <div className="app-shell">
                <header className="topbar">
                    <div className="brand">
                        <span className="brand-mark">F</span>
                        <span className="brand-name">FEI</span>
                    </div>
                    <nav className="application-menu" aria-label="Application menu">
                        <DropdownMenu.Root>
                            <DropdownMenu.Trigger asChild><button className="menu-trigger" type="button">File</button></DropdownMenu.Trigger>
                            <DropdownMenu.Portal>
                                <DropdownMenu.Content className="menu-content" sideOffset={5} align="start">
                                    <DropdownMenu.Item
                                        className="menu-item"
                                        onSelect={() =>
                                            void openProjectFolder().catch((error) => {
                                                if (!(error instanceof DOMException) || error.name !== "AbortError") {
                                                    appendConsole("error", "project", errorMessage(error));
                                                }
                                            })
                                        }
                                    >
                                        Open Folder<span>Ctrl+O</span>
                                    </DropdownMenu.Item>
                                    <DropdownMenu.Item className="menu-item" disabled={!canEdit} onSelect={() => void saveActiveFile()}>
                                        Save<span>Ctrl+S</span>
                                    </DropdownMenu.Item>
                                    <DropdownMenu.Separator className="menu-separator" />
                                    <DropdownMenu.Item
                                        className="menu-item"
                                        disabled={!storage.isOpen}
                                        onSelect={() =>
                                            void showProjectSettings().catch((error) =>
                                                appendConsole("error", "project", errorMessage(error)),
                                            )
                                        }
                                    >
                                        Project Settings…
                                    </DropdownMenu.Item>
                                </DropdownMenu.Content>
                            </DropdownMenu.Portal>
                        </DropdownMenu.Root>
                        <DropdownMenu.Root>
                            <DropdownMenu.Trigger asChild><button className="menu-trigger" type="button">View</button></DropdownMenu.Trigger>
                            <DropdownMenu.Portal>
                                <DropdownMenu.Content className="menu-content" sideOffset={5} align="start">
                                    <DropdownMenu.Item className="menu-item" onSelect={resetWorkbenchLayout}>
                                        Reset Workbench Layout
                                    </DropdownMenu.Item>
                                </DropdownMenu.Content>
                            </DropdownMenu.Portal>
                        </DropdownMenu.Root>
                    </nav>
                    <div className="project-title" title={projectName}>{projectName}</div>
                    <div className="run-controls" aria-label="Runtime controls">
                        <IconButton
                            id="play"
                            label="Play"
                            disabled={!storage.isOpen || runtimeState === "starting" || runtimeState === "running"}
                            onClick={() => void playRuntime()}
                        >
                            <Play size={14} fill="currentColor" />
                        </IconButton>
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
                            <RotateCcw size={14} />
                        </IconButton>
                    </div>
                    <div className="toolbar" aria-label="Project controls">
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
                            <FolderOpen size={14} />
                            {openFolderLabel}
                        </button>
                        <IconButton label="Save (Ctrl+S)" disabled={!canEdit} onClick={() => void saveActiveFile()}>
                            <Save size={14} />
                        </IconButton>
                        <div className={`runtime-badge ${runtimeState}`}>
                            <span className="status-dot" />
                            {runtimeState}
                        </div>
                    </div>
                </header>

                <main className="workbench">
                    <WorkbenchPanelsContext.Provider value={workbenchPanels}>
                        <DockviewReact
                            className="dockview-theme-fei"
                            components={dockviewComponents}
                            tabComponents={dockviewTabComponents}
                            onReady={onDockviewReady}
                        />
                    </WorkbenchPanelsContext.Provider>
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
                                    ? "This removes the file from the project's asset directory."
                                    : "Paths are relative to the project’s Assets root."}
                            </Dialog.Description>
                            <label className="field-label" htmlFor="operation-path">Asset path</label>
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

                <Dialog.Root open={settingsOpen} onOpenChange={setSettingsOpen}>
                    <Dialog.Portal>
                        <Dialog.Overlay className="dialog-overlay" />
                        <Dialog.Content className="dialog-content settings-dialog">
                            <div className="dialog-heading">
                                <div>
                                    <Dialog.Title>Project Settings</Dialog.Title>
                                    <Dialog.Description>Configure project-level metadata managed in project.yaml.</Dialog.Description>
                                </div>
                                <Dialog.Close asChild>
                                    <button className="icon-button" type="button" aria-label="Close"><X size={15} /></button>
                                </Dialog.Close>
                            </div>
                            <div className="settings-form">
                                <label className="settings-field">
                                    <span>Project name</span>
                                    <input
                                        className="text-field"
                                        value={settingsDraft.name}
                                        autoFocus
                                        onChange={(event) =>
                                            setSettingsDraft((current) => ({ ...current, name: event.target.value }))
                                        }
                                    />
                                </label>
                                <details className="settings-advanced">
                                    <summary>Advanced</summary>
                                    <label className="settings-field">
                                        <span>Asset directory <small>Managed by the development editor</small></span>
                                        <input className="text-field" value={settingsDraft.assetDirectory} readOnly />
                                    </label>
                                    <label className="settings-field">
                                        <span>Runtime plugins <small>One qualified plugin id per line</small></span>
                                        <textarea
                                            className="text-area"
                                            rows={5}
                                            spellCheck={false}
                                            value={settingsPlugins}
                                            placeholder="project_runtime::LuauScripts"
                                            onChange={(event) => setSettingsPlugins(event.target.value)}
                                        />
                                    </label>
                                </details>
                            </div>
                            {settingsError && <p className="dialog-error">{settingsError}</p>}
                            <div className="dialog-actions">
                                <Dialog.Close asChild><button className="button" type="button">Cancel</button></Dialog.Close>
                                <button className="button primary" type="button" onClick={() => void applyProjectSettings()}>
                                    Save Settings
                                </button>
                            </div>
                        </Dialog.Content>
                    </Dialog.Portal>
                </Dialog.Root>

                <Dialog.Root open={agentSettingsOpen} onOpenChange={setAgentSettingsOpen}>
                    <Dialog.Portal>
                        <Dialog.Overlay className="dialog-overlay" />
                        <Dialog.Content className="dialog-content settings-dialog">
                            <div className="dialog-heading">
                                <div>
                                    <Dialog.Title>Agent Model</Dialog.Title>
                                    <Dialog.Description>
                                        The API key is encrypted by the local Editor Host and is never returned to the browser.
                                    </Dialog.Description>
                                </div>
                                <Dialog.Close asChild>
                                    <button className="icon-button" type="button" aria-label="Close"><X size={15} /></button>
                                </Dialog.Close>
                            </div>
                            <div className="settings-form">
                                <label className="settings-field">
                                    <span>DeepSeek API key</span>
                                    <input
                                        className="text-field"
                                        type="password"
                                        value={agentApiKey}
                                        autoFocus
                                        autoComplete="off"
                                        spellCheck={false}
                                        placeholder="Enter a new API key"
                                        onChange={(event) => setAgentApiKey(event.target.value)}
                                        onKeyDown={(event) => {
                                            if (event.key === "Enter" && agentApiKey.trim()) {
                                                void saveAgentApiKey();
                                            }
                                        }}
                                    />
                                </label>
                                <p className="muted-copy">
                                    Saving replaces the existing credential. The current key cannot be displayed.
                                </p>
                            </div>
                            {agentSettingsError && <p className="dialog-error">{agentSettingsError}</p>}
                            <div className="dialog-actions">
                                <button
                                    className="button danger"
                                    type="button"
                                    disabled={agentSettingsSaving || agentGatewayState.state !== "ready"}
                                    onClick={() => void removeAgentApiKey()}
                                >
                                    Remove Key
                                </button>
                                <Dialog.Close asChild><button className="button" type="button">Cancel</button></Dialog.Close>
                                <button
                                    className="button primary"
                                    type="button"
                                    disabled={agentSettingsSaving || !agentApiKey.trim()}
                                    onClick={() => void saveAgentApiKey()}
                                >
                                    {agentSettingsSaving ? "Saving…" : "Save Key"}
                                </button>
                            </div>
                        </Dialog.Content>
                    </Dialog.Portal>
                </Dialog.Root>
            </div>
        </Tooltip.Provider>
    );
}
