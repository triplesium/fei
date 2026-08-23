import {
    DockviewReact,
    type DockviewApi,
    type DockviewReadyEvent,
    type IDockviewPanelHeaderProps,
    type IDockviewPanelProps,
} from "dockview-react";
import {
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
    Settings2,
    Terminal,
    Trash2,
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
import type {
    AgentModelDraft,
    AgentModelEditorTarget,
} from "./components/agent-model-dialog";
import { AgentModelSelector } from "./components/agent-model-selector";
import { EditorTopbar } from "./components/editor-topbar";
import { SettingsDialog } from "./components/settings-dialog";
import { Badge } from "./components/ui/badge";
import { Button as UiButton } from "./components/ui/button";
import { IconButton } from "./components/ui/icon-button";
import { TooltipProvider } from "./components/ui/tooltip";
import { ScrollArea } from "./components/ui/scroll-area";
import { Separator } from "./components/ui/separator";
import { CodeEditor } from "./components/code-editor";
import { RuntimeViewport } from "./components/runtime-viewport";
import {
    PanelEmptyState,
    PanelSection,
    PanelSectionTitle,
    PanelStatus,
    PanelToolbar,
    ToolPanel,
} from "./components/panel";
import {
    ProjectOperationDialog,
    type ProjectOperation,
} from "./components/project-operation-dialog";
import { ProjectSettingsDialog } from "./components/project-settings-dialog";
import { WasmRuntimeController } from "./runtime/wasm-runtime-controller";
import { cn } from "./lib/utils";
import {
    editorHost,
    type EditorModelSettingsUpdate,
    type EditorSettings,
} from "./services/editor-host-client";
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
const defaultAgentModelDraft: AgentModelDraft = {
    providerId: "",
    modelId: "",
    providerName: "Custom API",
    baseUrl: "http://127.0.0.1:8000/v1",
    api: "responses",
    modelName: "Custom Model",
    reasoning: false,
    contextWindow: "128000",
    maxTokens: "8192",
    apiKey: "",
};

const defaultEditorSettings: EditorSettings = {
    version: 1,
    appearance: { agentDensity: "compact" },
};

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
                        <div
                            className="flex h-7 items-center gap-2 text-[11px] font-semibold text-[#b7bdc8]"
                            style={{ paddingLeft: 8 + depth * 14 }}
                        >
                            <FolderOpen className="shrink-0 text-muted-foreground" size={14} strokeWidth={1.6} />
                            <span className="truncate">{node.name}</span>
                        </div>
                        {renderNodes(node.children, depth + 1)}
                    </div>
                );
            }
            return (
                <UiButton
                    key={node.path}
                    variant="ghost"
                    size="sm"
                    className={cn(
                        "h-7 w-full justify-start gap-2 px-2 font-normal text-[#b7c2d0] hover:bg-muted",
                        node.entry.path === activePath &&
                            "bg-[#313844] text-foreground shadow-[inset_2px_0_var(--accent)] hover:bg-[#313844]",
                    )}
                    type="button"
                    disabled={node.entry.readonly}
                    aria-current={node.entry.path === activePath ? "page" : undefined}
                    style={{ paddingLeft: 10 + depth * 14 }}
                    onClick={() => onSelect(node.entry!.path)}
                >
                    <span className="shrink-0 text-primary">{fileIcon(node.entry)}</span>
                    <span className="min-w-0 flex-1 truncate text-left">{node.name}</span>
                    {node.entry.readonly && <Badge variant="outline">BIN</Badge>}
                </UiButton>
            );
        });
    return <>{renderNodes(buildFileTree(files))}</>;
}

type CommandHandler = (request: AgentRequest) => Promise<unknown>;

const workbenchLayoutStorageKey = "entisium-editor-dockview-layout-v2";
const dockviewComponents = { panel: DockPanel };
const dockviewTabComponents = { engine: EnginePanelTab };
const WorkbenchPanelsContext = createContext<Record<string, ReactNode>>({});

function DockPanel({ api }: IDockviewPanelProps) {
    const panels = useContext(WorkbenchPanelsContext);
    return panels[api.id] ?? <PanelEmptyState className="bg-card">Panel unavailable</PanelEmptyState>;
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
        <div className="flex h-full min-w-0 items-center gap-1.5 px-2.5 uppercase tracking-[0.07em]">
            <Icon className="shrink-0 text-muted-foreground [.dv-active-tab_&]:text-primary" size={13} strokeWidth={1.7} />
            <span className="truncate">{api.title}</span>
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
    const [operation, setOperation] = useState<ProjectOperation>(null);
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
    const [globalSettingsOpen, setGlobalSettingsOpen] = useState(false);
    const [editorPreferences, setEditorPreferences] = useState<EditorSettings>(defaultEditorSettings);
    const [agentModelDraft, setAgentModelDraft] = useState<AgentModelDraft>(defaultAgentModelDraft);
    const [agentModelTarget, setAgentModelTarget] = useState<AgentModelEditorTarget | null>(null);
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

    const showOperation = (next: Exclude<ProjectOperation, null>): void => {
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
                document.documentElement.dataset.entisiumEditorReady = "true";
            })
            .catch((error) => {
                document.documentElement.dataset.entisiumEditorReady = "failed";
                appendConsole("error", "editor", errorMessage(error));
            });
        return () => {
            cancelled = true;
        };
        // Initialization intentionally runs once for the process-wide storage service.
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, []);

    useEffect(() => {
        document.documentElement.dataset.entisiumEditorRuntime = runtimeState;
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
            if ((event.ctrlKey || event.metaKey) && event.key === ",") {
                event.preventDefault();
                setAgentSettingsError("");
                setAgentModelTarget(null);
                setGlobalSettingsOpen(true);
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
        window.entisiumEditorAgent = agentApi;
    }, [agentApi]);

    const piAgent = useMemo(() => new EditorPiAgent(agentApi), [agentApi]);
    const modelGateway = useMemo(() => new EditorModelGateway(), []);

    useEffect(() => {
        window.entisiumEditorPi = piAgent;
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

    useEffect(() => {
        let cancelled = false;
        void editorHost
            .getEditorSettings()
            .then((settings) => {
                if (!cancelled) setEditorPreferences(settings);
            })
            .catch((error) => {
                if (!cancelled) appendConsole("error", "settings", errorMessage(error));
            });
        return () => {
            cancelled = true;
        };
    }, [appendConsole]);

    const openAgentSettings = (): void => {
        setAgentSettingsError("");
        setAgentModelTarget(null);
        setGlobalSettingsOpen(true);
    };

    const editAgentModel = (providerId: string, modelId: string): void => {
        const settings = "settings" in agentGatewayState ? agentGatewayState.settings : undefined;
        const provider = settings?.providers.find((candidate) => candidate.id === providerId);
        const model = provider?.models.find((candidate) => candidate.id === modelId);
        if (!provider || !model) return;
        setAgentModelTarget({ kind: "model", providerId, modelId });
        setAgentModelDraft({
            providerId,
            modelId,
            providerName: provider.name,
            baseUrl: provider.baseUrl,
            api: provider.api,
            modelName: model.name,
            reasoning: model.reasoning,
            contextWindow: String(model.contextWindow),
            maxTokens: String(model.maxTokens),
            apiKey: "",
        });
        setAgentSettingsError("");
    };

    const addAgentModel = (providerId: string): void => {
        const settings = "settings" in agentGatewayState ? agentGatewayState.settings : undefined;
        const provider = settings?.providers.find((candidate) => candidate.id === providerId);
        if (!provider) return;
        setAgentModelTarget({ kind: "model", providerId });
        setAgentModelDraft({
            ...defaultAgentModelDraft,
            providerId,
            providerName: provider.name,
            baseUrl: provider.baseUrl,
            api: provider.api,
        });
        setAgentSettingsError("");
    };

    const addAgentProvider = (): void => {
        const providerId = `provider-${crypto.randomUUID().slice(0, 8)}`;
        setAgentModelTarget({ kind: "provider", providerId, newProvider: true });
        setAgentModelDraft({ ...defaultAgentModelDraft, providerId });
        setAgentSettingsError("");
    };

    const editAgentProvider = (providerId: string): void => {
        const settings = "settings" in agentGatewayState ? agentGatewayState.settings : undefined;
        const provider = settings?.providers.find((candidate) => candidate.id === providerId);
        if (!provider) return;
        setAgentModelTarget({ kind: "provider", providerId, newProvider: false });
        setAgentModelDraft({
            ...defaultAgentModelDraft,
            providerId,
            providerName: provider.name,
            baseUrl: provider.baseUrl,
            api: provider.api,
        });
        setAgentSettingsError("");
    };

    const saveAgentProviderSettings = async (): Promise<void> => {
        if (agentModelTarget?.kind !== "provider") return;
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const newProvider = agentModelTarget.newProvider;
            const providerId = agentModelDraft.providerId.trim();
            const state = await modelGateway.configureProvider({
                provider: {
                    id: providerId,
                    name: agentModelDraft.providerName.trim(),
                    baseUrl: agentModelDraft.baseUrl.trim(),
                    api: agentModelDraft.api,
                },
                ...(agentModelDraft.apiKey.trim() ? { apiKey: agentModelDraft.apiKey.trim() } : {}),
            }, piAgent);
            setAgentGatewayState(state);
            setAgentModelDraft((current) => ({ ...current, apiKey: "" }));
            if (newProvider) {
                setAgentModelDraft((current) => ({
                    ...current,
                    modelId: "",
                    modelName: defaultAgentModelDraft.modelName,
                    reasoning: defaultAgentModelDraft.reasoning,
                    contextWindow: defaultAgentModelDraft.contextWindow,
                    maxTokens: defaultAgentModelDraft.maxTokens,
                }));
                setAgentModelTarget({ kind: "model", providerId });
            } else {
                setAgentModelTarget(null);
            }
            appendConsole("info", "agent", `saved provider ${providerId}`);
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const saveAgentModelSettings = async (): Promise<void> => {
        if (agentModelTarget?.kind !== "model") return;
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.configureRegistryModel({
                providerId: agentModelTarget.providerId,
                ...(agentModelTarget.modelId ? { previousModelId: agentModelTarget.modelId } : {}),
                model: {
                    id: agentModelDraft.modelId.trim(),
                    name: agentModelDraft.modelName.trim(),
                    reasoning: agentModelDraft.reasoning,
                    contextWindow: Number(agentModelDraft.contextWindow),
                    maxTokens: Number(agentModelDraft.maxTokens),
                },
            }, piAgent);
            setAgentGatewayState(state);
            setAgentModelTarget(null);
            appendConsole("info", "agent", `saved model ${agentModelTarget.providerId}/${agentModelDraft.modelId.trim()}`);
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const deleteAgentModel = async (providerId: string, modelId: string): Promise<void> => {
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.deleteModel(providerId, modelId, piAgent);
            setAgentGatewayState(state);
            setAgentModelTarget(null);
            appendConsole("info", "agent", `deleted model ${providerId}/${modelId}`);
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const deleteAgentProvider = async (providerId: string): Promise<void> => {
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.deleteProvider(providerId, piAgent);
            setAgentGatewayState(state);
            setAgentModelTarget(null);
            appendConsole("info", "agent", `deleted provider ${providerId}`);
        } catch (error) {
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const removeAgentCredential = async (): Promise<void> => {
        setAgentSettingsSaving(true);
        setAgentSettingsError("");
        try {
            const state = await modelGateway.removeCredential(agentModelDraft.providerId, piAgent);
            setAgentGatewayState(state);
            setAgentModelDraft((current) => ({ ...current, apiKey: "" }));
            appendConsole("info", "agent", "model credential removed");
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
                console.warn("[entisium editor] discarded invalid workbench layout", error);
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
    const agentModelSettings =
        "settings" in agentGatewayState ? agentGatewayState.settings : undefined;
    const draftModelProvider = agentModelSettings?.providers.find(
        (provider) => provider.id === agentModelDraft.providerId,
    );
    const draftCredentialConfigured = draftModelProvider?.configured ?? false;
    const agentProviderDraftValid =
        agentModelDraft.providerId.trim().length > 0 &&
        agentModelDraft.providerName.trim().length > 0 &&
        agentModelDraft.baseUrl.trim().length > 0;
    const agentModelDraftValid =
        agentModelDraft.modelId.trim().length > 0 &&
        agentModelDraft.modelName.trim().length > 0 &&
        Number.isInteger(Number(agentModelDraft.contextWindow)) &&
        Number.isInteger(Number(agentModelDraft.maxTokens));
    const selectAgentModel = async (providerId: string, modelId: string): Promise<void> => {
        if (agentStreaming || agentSettingsSaving) return;
        const provider = agentModelSettings?.providers.find((candidate) => candidate.id === providerId);
        const model = provider?.models.find((candidate) => candidate.id === modelId);
        if (!provider || !model) return;
        if (!provider.configured) {
            openAgentSettings();
            editAgentProvider(providerId);
            return;
        }
        if (
            "providerId" in agentGatewayState &&
            agentGatewayState.providerId === providerId &&
            agentGatewayState.modelId === modelId
        ) {
            return;
        }

        const update: EditorModelSettingsUpdate = { providerId, modelId };

        setAgentSettingsSaving(true);
        try {
            const state = await modelGateway.configure(update, piAgent);
            setAgentGatewayState(state);
            appendConsole("info", "agent", `using ${provider.name} · ${model.name}`);
        } catch (error) {
            appendConsole("error", "agent", errorMessage(error));
            openAgentSettings();
            editAgentModel(providerId, modelId);
            setAgentSettingsError(errorMessage(error));
        } finally {
            setAgentSettingsSaving(false);
        }
    };

    const activeAgentProviderId =
        "providerId" in agentGatewayState ? agentGatewayState.providerId : undefined;
    const activeAgentModelId =
        "modelId" in agentGatewayState ? agentGatewayState.modelId : undefined;
    const agentModelControl = (
        <AgentModelSelector
            providers={agentModelSettings?.providers}
            activeProviderId={activeAgentProviderId}
            activeModelId={activeAgentModelId}
            label={
                agentGatewayState.state === "ready" || agentGatewayState.state === "unconfigured"
                    ? agentGatewayState.model
                    : "Select model"
            }
            ready={agentGatewayState.state === "ready"}
            disabled={agentStreaming || agentSettingsSaving}
            onSelectModel={selectAgentModel}
            onManageModels={openAgentSettings}
        />
    );

    useEffect(() => {
        dockviewApiRef.current
            ?.getPanel("code")
            ?.api.setTitle(
                activePath ? `${assetRelativePath(activePath)}${dirty ? " •" : ""}` : "Code",
            );
    }, [activePath, dirty]);

    const workbenchPanels: Record<string, ReactNode> = {
        project: (
            <ToolPanel>
                <PanelToolbar>
                    <span>{files.length > 0 ? `${files.length} files` : storageLabel}</span>
                    <div className="flex items-center gap-px">
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
                </PanelToolbar>
                <ScrollArea className="min-h-0 flex-1">
                    <nav className="min-h-full p-1.5" aria-label="Project files">
                        {!storage.isOpen ? (
                            <PanelEmptyState>
                                <Folder size={28} strokeWidth={1.3} />
                                <span>Open a local project folder to begin.</span>
                            </PanelEmptyState>
                        ) : (
                            <FileTree files={files} activePath={activePath} onSelect={(path) => void selectFile(path)} />
                        )}
                    </nav>
                </ScrollArea>
                <PanelStatus>
                    <span className="truncate" title={storageLabel}>{storageLabel}</span>
                    <span className={dirty ? "text-[#fbbf24]" : ""}>{canEdit ? (dirty ? "Unsaved" : "Saved") : "—"}</span>
                </PanelStatus>
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
                <PanelStatus className="justify-end">
                    <span>{dirty ? "● Unsaved" : "Saved"}</span>
                    <span>Ln {cursor.line}, Col {cursor.column}</span>
                    <span>{languageForPath(activePath)}</span>
                    <span>UTF-8</span>
                </PanelStatus>
            </ToolPanel>
        ),
        console: (
            <ToolPanel>
                <PanelToolbar>
                    <span>{logs.length} messages</span>
                    <UiButton variant="ghost" size="sm" className="h-6" type="button" onClick={() => setLogs([])}>
                        Clear
                    </UiButton>
                </PanelToolbar>
                <ScrollArea viewportRef={consoleRef} className="min-h-0 flex-1 bg-[#0a0e13]">
                    <div className="min-h-full px-2.5 py-1.5 font-mono text-[10px] leading-[1.5]" aria-live="polite">
                        {logs.length === 0 && <PanelEmptyState>No console output.</PanelEmptyState>}
                        {logs.map((entry) => (
                            <div
                                key={entry.id}
                                className="grid grid-cols-[58px_52px_minmax(0,1fr)] gap-2 py-0.5 text-[#b5c0ce]"
                            >
                                <span className="text-[#586579]">{entry.time}</span>
                                <span className="text-[#586579]">{entry.source}</span>
                                <span className={cn(entry.level === "error" && "text-[#ff9aaa]", entry.level === "command" && "text-[#7eacff]")}>{entry.message}</span>
                            </div>
                        ))}
                    </div>
                </ScrollArea>
            </ToolPanel>
        ),
        game: (
            <RuntimeViewport
                state={runtimeState}
                detail={runtimeDetail}
                session={runtimeSession}
                projectOpen={storage.isOpen}
                onFrame={attachRuntimeFrame}
                onPlay={() => {
                    void playRuntime().catch((error) =>
                        appendConsole("error", "runtime", errorMessage(error)),
                    );
                }}
                onRestart={() => {
                    void restartRuntime().catch((error) =>
                        appendConsole("error", "runtime", errorMessage(error)),
                    );
                }}
            />
        ),
        agent: (
            <ToolPanel className="min-h-0 bg-[#0d1117]">
                <PiAssistantThread
                    agent={piAgent}
                    enabled={agentGatewayState.state === "ready"}
                    modelControl={agentModelControl}
                    density={editorPreferences.appearance.agentDensity}
                    onConfigure={openAgentSettings}
                    onNewChat={resetAgentConversation}
                />
            </ToolPanel>
        ),
        inspector: (
            <ToolPanel>
                <ScrollArea className="min-h-0 flex-1">
                    <PanelSection>
                        <PanelSectionTitle>RUNTIME</PanelSectionTitle>
                        <dl className="mt-2">
                            {[["State", runtimeDetail], ["Script", runtimeScript], ["Frame", runtimeFrame]].map(([label, value]) => (
                                <div key={label} className="grid grid-cols-[62px_minmax(0,1fr)] border-b border-border/55 py-1.5 text-[10px]">
                                    <dt className="text-muted-foreground">{label}</dt>
                                    <dd className="m-0 truncate text-right text-[#b7bdc8]">{value}</dd>
                                </div>
                            ))}
                        </dl>
                    </PanelSection>
                    <Separator />
                    <PanelSection>
                        <PanelSectionTitle>AGENT API</PanelSectionTitle>
                        <p className="text-[10px] leading-relaxed text-muted-foreground">UI and agents use the same command bus through <code className="font-mono text-primary">window.entisiumEditorAgent</code>.</p>
                        <div className="flex flex-wrap gap-1">
                            {agentApi.capabilities.map((capability) => <Badge key={capability}>{capability}</Badge>)}
                        </div>
                    </PanelSection>
                    <Separator />
                    <PanelSection>
                        <PanelSectionTitle>PI TOOLS</PanelSectionTitle>
                        <p className="text-[10px] leading-relaxed text-muted-foreground">
                            {agentGatewayState.state === "ready"
                                ? `${agentGatewayState.provider} · ${agentGatewayState.model}`
                                : agentGatewayState.state === "unconfigured"
                                  ? `${agentGatewayState.provider} credential required`
                                  : agentGatewayState.state === "unavailable"
                                    ? "Local model gateway unavailable"
                                    : "Connecting to local model gateway…"}
                        </p>
                        <div className="flex flex-wrap gap-1">
                            {piAgent.status().tools.map((tool) => <Badge key={tool}>{tool}</Badge>)}
                        </div>
                        <UiButton
                            variant="secondary"
                            size="sm"
                            className="mt-2"
                            type="button"
                            onClick={() => {
                                openAgentSettings();
                            }}
                        >
                            Configure Model
                        </UiButton>
                    </PanelSection>
                </ScrollArea>
            </ToolPanel>
        ),
    };

    return (
        <TooltipProvider delayDuration={450}>
            <div className="flex size-full flex-col overflow-hidden bg-background">
                <EditorTopbar
                    projectName={projectName}
                    openFolderLabel={openFolderLabel}
                    projectOpen={storage.isOpen}
                    canSave={canEdit}
                    runtimeState={runtimeState}
                    onOpenProject={() => {
                        void openProjectFolder().catch((error) => {
                            if (!(error instanceof DOMException) || error.name !== "AbortError") {
                                appendConsole("error", "project", errorMessage(error));
                            }
                        });
                    }}
                    onSave={() => {
                        void saveActiveFile().catch((error) =>
                            appendConsole("error", "editor", errorMessage(error)),
                        );
                    }}
                    onOpenProjectSettings={() => {
                        void showProjectSettings().catch((error) =>
                            appendConsole("error", "project", errorMessage(error)),
                        );
                    }}
                    onOpenSettings={openAgentSettings}
                    onResetWorkbench={resetWorkbenchLayout}
                    onPlay={() => {
                        void playRuntime().catch((error) =>
                            appendConsole("error", "runtime", errorMessage(error)),
                        );
                    }}
                    onStop={() => stopRuntime()}
                    onRestart={() => {
                        void restartRuntime().catch((error) =>
                            appendConsole("error", "runtime", errorMessage(error)),
                        );
                    }}
                />

                <main className="min-h-0 flex-1 bg-[#111318] p-[3px]">
                    <WorkbenchPanelsContext.Provider value={workbenchPanels}>
                        <DockviewReact
                            className="dockview-theme-entisium"
                            components={dockviewComponents}
                            tabComponents={dockviewTabComponents}
                            onReady={onDockviewReady}
                        />
                    </WorkbenchPanelsContext.Provider>
                </main>

                <ProjectOperationDialog
                    operation={operation}
                    path={operationPath}
                    error={operationError || undefined}
                    onOpenChange={(open) => {
                        if (!open) setOperation(null);
                    }}
                    onPathChange={setOperationPath}
                    onApply={applyOperation}
                />

                <ProjectSettingsDialog
                    open={settingsOpen}
                    draft={settingsDraft}
                    plugins={settingsPlugins}
                    error={settingsError || undefined}
                    onOpenChange={setSettingsOpen}
                    onNameChange={(name) =>
                        setSettingsDraft((current) => ({ ...current, name }))
                    }
                    onPluginsChange={setSettingsPlugins}
                    onSave={applyProjectSettings}
                />

                <SettingsDialog
                    open={globalSettingsOpen}
                    onOpenChange={setGlobalSettingsOpen}
                    draft={agentModelDraft}
                    target={agentModelTarget}
                    providers={agentModelSettings?.providers}
                    activeProviderId={activeAgentProviderId}
                    activeModelId={activeAgentModelId}
                    credentialConfigured={draftCredentialConfigured}
                    providerValid={agentProviderDraftValid}
                    modelValid={agentModelDraftValid}
                    saving={agentSettingsSaving}
                    error={agentSettingsError || undefined}
                    onDraftChange={(patch) =>
                        setAgentModelDraft((current) => ({ ...current, ...patch }))
                    }
                    onActivate={selectAgentModel}
                    onAddProvider={addAgentProvider}
                    onAddModel={addAgentModel}
                    onEditProvider={editAgentProvider}
                    onEditModel={editAgentModel}
                    onBack={() => {
                        setAgentModelTarget(null);
                        setAgentSettingsError("");
                    }}
                    onSaveProvider={saveAgentProviderSettings}
                    onSaveModel={saveAgentModelSettings}
                    onDeleteProvider={deleteAgentProvider}
                    onDeleteModel={deleteAgentModel}
                    onRemoveCredential={removeAgentCredential}
                />
            </div>
        </TooltipProvider>
    );
}
