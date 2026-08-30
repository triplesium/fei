import {
    DockviewReact,
    type DockviewApi,
    type DockviewReadyEvent,
    type IDockviewPanelHeaderProps,
    type IDockviewPanelProps,
} from "dockview-react";
import {
    Activity,
    Bot,
    ChevronDown,
    ChevronRight,
    Code2,
    File,
    FileCode2,
    FileJson,
    Folder,
    FolderOpen,
    FolderPlus,
    Image,
    Pencil,
    Play,
    Plus,
    RefreshCw,
    Settings2,
    Terminal,
    Trash2,
    X,
} from "lucide-react";
import {
    hotkeysCoreFeature,
    selectionFeature,
    syncDataLoaderFeature,
} from "@headless-tree/core";
import { useTree } from "@headless-tree/react";
import {
    createContext,
    useCallback,
    useContext,
    useEffect,
    useLayoutEffect,
    useMemo,
    useRef,
    useState,
    type ReactNode,
} from "react";
import { parseDocument } from "yaml";
import {
    AgentModelSelector,
    EditorModelGateway,
    EditorPiAgent,
    PiAssistantThread,
    SettingsDialog,
    connectEditorCommandBridge,
    editorToolRegistry,
    type EditorCommandHandler,
    editorSettings,
    AgentModelDraft,
    AgentModelEditorTarget,
    type EditorModelSettingsUpdate,
    type EditorSettings,
    type ModelGatewayState,
} from "@editor-platform/agent";
import { EditorTopbar } from "./components/editor-topbar";
import { ResourceInspector } from "./components/resource-inspector";
import { Button as UiButton } from "./components/ui/button";
import {
    ContextMenu,
    ContextMenuContent,
    ContextMenuItem,
    ContextMenuSeparator,
    ContextMenuTrigger,
} from "./components/ui/context-menu";
import { IconButton } from "./components/ui/icon-button";
import { TooltipProvider } from "./components/ui/tooltip";
import { ScrollArea } from "./components/ui/scroll-area";
import { CodeEditor } from "./components/code-editor";
import { RuntimeViewport } from "./components/runtime-viewport";
import { ProfilerPanel } from "./components/profiler-panel";
import {
    PanelEmptyState,
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
import {
    finalizeProfileCapture,
    type ProfileCaptureArchive,
    type ProfileCaptureProgress,
} from "./runtime/profile-capture-archive";
import {
    readProfilerFrame,
    readProfilerFrames,
    readProfilerSummary,
    type ProfilerAgentSource,
} from "./services/profiler-agent";
import { cn } from "./lib/utils";
import { editorCapabilities } from "@editor-platform/capabilities";
import {
    synchronizeLuauLanguageClientProject,
    type LuauLanguageClientStatus,
    type LuauProjectFile,
} from "@editor-platform/luau-language-client";
import { ProjectStorage } from "@editor-platform/project-storage";
import type {
    AgentRequest,
    ConsoleEntry,
    ConsoleLevel,
    EditorAgentApi,
    ProjectAssetInspection,
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
    selectedPath,
    onSelect,
    onInspect,
}: {
    files: ProjectFileEntry[];
    selectedPath: string;
    onSelect(path: string): void;
    onInspect(path: string): void;
}) {
    const rootId = "__assets_root__";
    const treeData = useMemo(() => {
        const rootNodes = buildFileTree(files);
        const items = new Map<string, FileTreeNode>();
        const register = (nodes: FileTreeNode[]) => {
            for (const node of nodes) {
                items.set(node.path, node);
                register(node.children);
            }
        };
        register(rootNodes);
        items.set(rootId, {
            name: "Assets",
            path: rootId,
            children: rootNodes,
        });
        return {
            items,
            expandedItems: [...items.values()]
                .filter((item) => item.path !== rootId && !item.entry)
                .map((item) => item.path),
        };
    }, [files]);
    const selectedItemId = useMemo(
        () =>
            [...treeData.items.values()].find((item) => item.entry?.path === selectedPath)
                ?.path,
        [selectedPath, treeData],
    );
    // Headless Tree renders its cached item IDs once before rebuilding for new data.
    const previousItemsRef = useRef(treeData.items);

    const tree = useTree<FileTreeNode>({
        rootItemId: rootId,
        initialState: {
            expandedItems: treeData.expandedItems,
            selectedItems: selectedItemId ? [selectedItemId] : [],
        },
        state: {
            selectedItems: selectedItemId ? [selectedItemId] : [],
        },
        getItemName: (item) => item.getItemData().name,
        isItemFolder: (item) => {
            const entry = item.getItemData().entry;
            return !entry || entry.kind === "directory";
        },
        dataLoader: {
            getItem: (itemId) =>
                treeData.items.get(itemId) ?? previousItemsRef.current.get(itemId)!,
            getChildren: (itemId) =>
                treeData.items.get(itemId)?.children.map((child) => child.path) ?? [],
        },
        onPrimaryAction: (item) => {
            const entry = item.getItemData().entry;
            if (!entry) return;
            onInspect(entry.path);
            if (entry.kind !== "directory" && !entry.readonly) onSelect(entry.path);
        },
        features: [syncDataLoaderFeature, selectionFeature, hotkeysCoreFeature],
    });

    useLayoutEffect(() => {
        tree.rebuildTree();
        previousItemsRef.current = treeData.items;
    }, [tree, treeData]);

    return (
        <div {...tree.getContainerProps("Project files")} className="min-h-full py-1 outline-none">
            {tree.getItems().map((item) => {
                const node = item.getItemData();
                const entry = node.entry;
                const active = entry?.path === selectedPath;
                return (
                    <button
                        {...item.getProps()}
                        key={item.getKey()}
                        type="button"
                        aria-current={active ? "page" : undefined}
                        aria-disabled={entry?.readonly || undefined}
                        data-asset-operation-path={entry?.path}
                        className={cn(
                            "mx-1 flex h-7 w-[calc(100%-8px)] items-center gap-1 border-0 bg-transparent pr-2 text-left text-[12px] text-[#bdbdbd] outline-none transition-colors hover:bg-[#333] focus-visible:bg-[#363636]",
                            item.isFolder() && "font-medium text-[#c7c7c7]",
                            active &&
                                "rounded-sm bg-[#464646] text-[#eeeeee] hover:bg-[#4a4a4a]",
                            entry?.readonly && "text-[#929292]",
                        )}
                        style={{ paddingLeft: 5 + item.getItemMeta().level * 14 }}
                    >
                        <span className="grid size-3.5 shrink-0 place-items-center text-muted-foreground">
                            {item.isFolder() &&
                                (item.isExpanded() ? (
                                    <ChevronDown size={13} strokeWidth={1.8} />
                                ) : (
                                    <ChevronRight size={13} strokeWidth={1.8} />
                                ))}
                        </span>
                        <span
                            className={cn(
                                "grid size-4 shrink-0 place-items-center text-[#8d9cac]",
                                active && "text-[#62b3ff]",
                            )}
                        >
                            {item.isFolder() ? (
                                <FolderOpen size={14} strokeWidth={1.6} />
                            ) : entry ? (
                                fileIcon(entry)
                            ) : (
                                <File size={14} strokeWidth={1.6} />
                            )}
                        </span>
                        <span className="min-w-0 flex-1 truncate">{node.name}</span>
                    </button>
                );
            })}
        </div>
    );
}

function CodeFileTabs({
    paths,
    activePath,
    dirty,
    onSelect,
    onClose,
}: {
    paths: string[];
    activePath: string;
    dirty: boolean;
    onSelect(path: string): void;
    onClose(path: string): void;
}) {
    if (paths.length === 0) return null;
    return (
        <div className="flex h-8 shrink-0 items-stretch overflow-x-auto bg-[#1d1d1d]">
            {paths.map((path) => {
                const active = path === activePath;
                const label = assetRelativePath(path).split("/").at(-1) ?? path;
                return (
                    <div
                        key={path}
                        title={assetRelativePath(path)}
                        className={cn(
                            "group/file-tab flex min-w-0 max-w-52 shrink-0 items-center rounded-t bg-transparent text-[12px] text-muted-foreground transition-colors hover:bg-white/[0.04] hover:text-foreground",
                            active && "bg-[#191919] text-foreground hover:bg-[#191919]",
                        )}
                    >
                        <button
                            type="button"
                            className="m-0 flex min-w-0 flex-1 items-center gap-1.5 self-stretch border-0 bg-transparent py-0 pr-1 pl-2.5 text-inherit outline-none focus-visible:ring-1 focus-visible:ring-inset focus-visible:ring-ring/50"
                            aria-current={active ? "page" : undefined}
                            onClick={() => onSelect(path)}
                        >
                            <FileCode2 className={cn("size-3.5 shrink-0", active && "text-primary")} />
                            <span className="min-w-0 flex-1 truncate">{label}</span>
                            {active && dirty && <span className="size-1.5 shrink-0 rounded-full bg-primary" aria-label="Unsaved" />}
                        </button>
                        <button
                            type="button"
                            aria-label={`Close ${label}`}
                            className="mr-1 grid size-5 shrink-0 place-items-center rounded border-0 bg-transparent p-0 text-muted-foreground opacity-0 outline-none transition-[opacity,color,background-color] hover:bg-white/10 hover:text-foreground focus-visible:opacity-100 focus-visible:ring-1 focus-visible:ring-ring/50 group-hover/file-tab:opacity-100"
                            onClick={(event) => {
                                event.stopPropagation();
                                onClose(path);
                            }}
                        >
                            <X className="size-3" />
                        </button>
                    </div>
                );
            })}
        </div>
    );
}

const workbenchLayoutStorageKey = editorCapabilities.agent
    ? "entisium-editor-dockview-layout-v3"
    : "entisium-editor-dockview-layout-browser-v1";
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
                : api.id === "profiler"
                  ? Activity
                : api.id === "inspector"
                  ? Settings2
                  : Terminal;
    return (
        <div className="flex h-full min-w-0 items-center gap-1.5 px-2.5 font-medium tracking-[0.01em]">
            <Icon className="shrink-0 text-muted-foreground [.dv-active-tab_&]:text-foreground" size={14} strokeWidth={1.8} />
            <span className="truncate">{api.title}</span>
        </div>
    );
}

function addDefaultWorkbenchPanels(api: DockviewApi): void {
    if (editorCapabilities.agent) {
        api.addPanel({
            id: "agent",
            component: "panel",
            tabComponent: "engine",
            title: "Agent",
            renderer: "always",
            initialWidth: 400,
            minimumWidth: 320,
            maximumWidth: 560,
        });
    }
    api.addPanel({
        id: "code",
        component: "panel",
        tabComponent: "engine",
        title: "Code",
        ...(editorCapabilities.agent
            ? { position: { referencePanel: "agent", direction: "right" as const } }
            : {}),
        initialWidth: 760,
        minimumWidth: 440,
    });
    api.addPanel({
        id: "game",
        component: "panel",
        tabComponent: "engine",
        title: "Game",
        renderer: "always",
        inactive: true,
        position: { referencePanel: "code", direction: "within" },
    });
    api.addPanel({
        id: "project",
        component: "panel",
        tabComponent: "engine",
        title: "Assets",
        position: { referencePanel: "code", direction: "right" },
        initialWidth: 260,
        minimumWidth: 210,
        maximumWidth: 360,
    });
    api.addPanel({
        id: "inspector",
        component: "panel",
        tabComponent: "engine",
        title: "Inspector",
        position: { referencePanel: "project", direction: "below" },
        initialHeight: 320,
        minimumWidth: 210,
        minimumHeight: 160,
    });
    api.addPanel({
        id: "console",
        component: "panel",
        tabComponent: "engine",
        title: "Console",
        position: { referencePanel: "code", direction: "below" },
        initialHeight: 280,
        minimumHeight: 100,
        maximumHeight: 520,
    });
    addProfilerWorkbenchPanel(api);

    const agentWidth = Math.max(340, Math.min(440, Math.round(api.width * 0.28)));
    const inspectorHeight = Math.max(220, Math.min(360, Math.round(api.height * 0.42)));
    if (editorCapabilities.agent) {
        api.getPanel("agent")?.group.api.setSize({ width: agentWidth });
    }
    api.getPanel("project")?.group.api.setSize({ width: 260 });
    api.getPanel("console")?.group.api.setSize({ height: 280 });
    api.getPanel("inspector")?.group.api.setSize({ height: inspectorHeight });
    api.getPanel("code")?.api.setActive();
}

function addProfilerWorkbenchPanel(api: DockviewApi): boolean {
    if (api.getPanel("profiler")) return false;
    api.addPanel({
        id: "profiler",
        component: "panel",
        tabComponent: "engine",
        title: "Profiler",
        inactive: true,
        position: {
            referencePanel: api.getPanel("console") ? "console" : "code",
            direction: api.getPanel("console") ? "within" : "below",
        },
        initialHeight: 280,
        minimumHeight: 180,
        maximumHeight: 520,
    });
    return true;
}

export function App() {
    const runtimeControllerRef = useRef<WasmRuntimeController | null>(null);
    if (runtimeControllerRef.current === null) {
        runtimeControllerRef.current = new WasmRuntimeController();
    }
    const runtimeController = runtimeControllerRef.current;

    const [files, setFiles] = useState<ProjectFileEntry[]>([]);
    const [selectedAssetPath, setSelectedAssetPath] = useState("");
    const [assetInspection, setAssetInspection] = useState<ProjectAssetInspection | null>(null);
    const [assetInspectionLoading, setAssetInspectionLoading] = useState(false);
    const [assetInspectionError, setAssetInspectionError] = useState("");
    const [assetPreviewUrl, setAssetPreviewUrl] = useState("");
    const [activePath, setActivePath] = useState("");
    const [openPaths, setOpenPaths] = useState<string[]>([]);
    const [content, setContent] = useState("");
    const [savedContent, setSavedContent] = useState("");
    const [cursor, setCursor] = useState({ line: 1, column: 1 });
    const [luauLspStatus, setLuauLspStatus] =
        useState<LuauLanguageClientStatus>("stopped");
    const [logs, setLogs] = useState<ConsoleEntry[]>([]);
    const [operation, setOperation] = useState<ProjectOperation>(null);
    const [assetContextPath, setAssetContextPath] = useState("");
    const [operationTargetPath, setOperationTargetPath] = useState("");
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
    const [profileArchive, setProfileArchive] = useState<ProfileCaptureArchive | null>(null);
    const [profileFinalizing, setProfileFinalizing] = useState(false);
    const [profileFinalizeProgress, setProfileFinalizeProgress] =
        useState<ProfileCaptureProgress | null>(null);
    const consoleRef = useRef<HTMLDivElement>(null);
    const handlersRef = useRef<Record<string, EditorCommandHandler>>({});
    const dockviewApiRef = useRef<DockviewApi | null>(null);
    const dockviewLayoutListenerRef = useRef<{ dispose(): void } | null>(null);
    const runtimeFocusedGameRef = useRef(false);
    const runtimeStopRef = useRef<Promise<void> | null>(null);
    const profileArchiveRef = useRef<ProfileCaptureArchive | null>(null);

    const storeProfileArchive = useCallback((archive: ProfileCaptureArchive | null) => {
        profileArchiveRef.current = archive;
        setProfileArchive(archive);
    }, []);

    const dirty = storage.isOpen && activePath.length > 0 && content !== savedContent;

    useEffect(() => {
        let cancelled = false;
        let previewUrl = "";
        setAssetInspectionError("");
        if (!selectedAssetPath) {
            setAssetInspection(null);
            setAssetPreviewUrl("");
            setAssetInspectionLoading(false);
            return;
        }
        setAssetInspectionLoading(true);
        void storage
            .inspect(selectedAssetPath)
            .then(async (inspection) => {
                if (inspection.assetType === "image" && inspection.mimeType) {
                    const bytes = await storage.bytes(selectedAssetPath);
                    previewUrl = URL.createObjectURL(
                        new Blob([bytes], { type: inspection.mimeType }),
                    );
                }
                if (cancelled) {
                    if (previewUrl) URL.revokeObjectURL(previewUrl);
                    return;
                }
                setAssetInspection(inspection);
                setAssetPreviewUrl(previewUrl);
            })
            .catch((error) => {
                if (!cancelled) {
                    setAssetInspection(null);
                    setAssetPreviewUrl("");
                    setAssetInspectionError(errorMessage(error));
                }
            })
            .finally(() => {
                if (!cancelled) setAssetInspectionLoading(false);
            });
        return () => {
            cancelled = true;
        };
    }, [selectedAssetPath, files]);

    useEffect(() => () => {
        if (assetPreviewUrl) URL.revokeObjectURL(assetPreviewUrl);
    }, [assetPreviewUrl]);

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
    const inspectRuntime = useCallback(
        (provider: string, schema: string, payload: unknown) =>
            runtimeController.inspect(provider, schema, payload),
        [runtimeController],
    );

    const refreshFiles = async (): Promise<ProjectFileEntry[]> => {
        if (!storage.isOpen) {
            setFiles([]);
            setSelectedAssetPath("");
            return [];
        }
        const entries = await storage.list();
        const assetEntries = entries.filter((entry) => entry.path.startsWith(assetPathPrefix));
        setFiles(assetEntries);
        return assetEntries;
    };

    const synchronizeProjectLanguageFiles = async (
        entries: readonly ProjectFileEntry[],
    ): Promise<void> => {
        if (!editorCapabilities.luauLspProjectSync) return;
        const files = await Promise.all(
            entries
                .filter(
                    (entry) =>
                        entry.kind === "text" &&
                        /\.(?:lua|luau)$/i.test(entry.path),
                )
                .map(async (entry): Promise<LuauProjectFile> => ({
                    path: entry.path,
                    content: (await storage.read(entry.path)) ?? "",
                })),
        );
        await synchronizeLuauLanguageClientProject(files);
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
        setSelectedAssetPath(path);
        dockviewApiRef.current?.getPanel("code")?.api.setActive();
        if (path === activePath) return;
        if (dirty) await saveActiveFile();
        const next = (await storage.read(path)) ?? "";
        setOpenPaths((current) => current.includes(path) ? current : [...current, path]);
        setActivePath(path);
        setContent(next);
        setSavedContent(next);
        setCursor({ line: 1, column: 1 });
    };

    const loadOpenedProject = async (): Promise<void> => {
        try {
            const settings = await readProjectSettings();
            if (!settings.name.trim()) throw new Error("Project name is required.");
        } catch (error) {
            appendConsole("error", "project", `invalid project settings: ${errorMessage(error)}`);
        }
        const entries = await refreshFiles();
        await synchronizeProjectLanguageFiles(entries);
        setOpenPaths([]);
        setSelectedAssetPath("");
        setActivePath("");
        setContent("");
        setSavedContent("");
        const preferred =
            entries.find((entry) => entry.path === "assets/main.luau") ??
            entries.find((entry) => entry.kind !== "directory" && !entry.readonly);
        if (!preferred) return;
        const next = (await storage.read(preferred.path)) ?? "";
        setSelectedAssetPath(preferred.path);
        setOpenPaths([preferred.path]);
        setActivePath(preferred.path);
        setContent(next);
        setSavedContent(next);
    };

    const stopRuntime = async (
        reason = "runtime stopped",
        log = true,
        retainProfile = true,
    ): Promise<void> => {
        if (runtimeStopRef.current) return runtimeStopRef.current;
        const operation = (async () => {
            const snapshot = runtimeController.getSnapshot();
            if (retainProfile && snapshot.state === "running" && snapshot.session) {
                setProfileFinalizing(true);
                setProfileFinalizeProgress(null);
                try {
                    const archive = await finalizeProfileCapture(
                        (provider, schema, payload) =>
                            runtimeController.inspect(provider, schema, payload),
                        snapshot.session.channelId,
                        { onProgress: setProfileFinalizeProgress },
                    );
                    storeProfileArchive(archive);
                    const retained = archive.details.size;
                    const expected = archive.history?.frames.length ?? 0;
                    appendConsole(
                        archive.complete ? "info" : "error",
                        "profiler",
                        `retained CPU details for ${retained} / ${expected} frames`,
                    );
                    for (const warning of archive.warnings) {
                        appendConsole("error", "profiler", warning);
                    }
                } catch (error) {
                    appendConsole(
                        "error",
                        "profiler",
                        `could not finalize capture: ${errorMessage(error)}`,
                    );
                } finally {
                    setProfileFinalizing(false);
                    setProfileFinalizeProgress(null);
                }
            }
            runtimeController.stop(reason, log);
        })();
        runtimeStopRef.current = operation;
        try {
            await operation;
        } finally {
            if (runtimeStopRef.current === operation) runtimeStopRef.current = null;
        }
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
        if (runtimeState !== "stopped") {
            await stopRuntime("project settings changed", true, false);
        }
        storeProfileArchive(null);
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
        await stopRuntime("project folder changed", true, false);
        storeProfileArchive(null);
        await loadOpenedProject();
        appendConsole("info", "project", `opened local folder ${name}`);
    };

    const refreshProjectFolder = async (): Promise<void> => {
        if (dirty) await saveActiveFile();
        const previous = activePath;
        const previousSelection = selectedAssetPath;
        const entries = await refreshFiles();
        await synchronizeProjectLanguageFiles(entries);
        const next =
            entries.find(
                (entry) =>
                    entry.path === previous &&
                    entry.kind !== "directory" &&
                    !entry.readonly,
            ) ?? entries.find((entry) => entry.kind !== "directory" && !entry.readonly);
        if (next) {
            const nextContent = (await storage.read(next.path)) ?? "";
            setOpenPaths((current) => {
                const available = new Set(entries.map((entry) => entry.path));
                const retained = current.filter((path) => available.has(path));
                return retained.includes(next.path) ? retained : [...retained, next.path];
            });
            setActivePath(next.path);
            setContent(nextContent);
            setSavedContent(nextContent);
        }
        setSelectedAssetPath(
            entries.some((entry) => entry.path === previousSelection)
                ? previousSelection
                : (next?.path ?? entries[0]?.path ?? ""),
        );
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
            setSelectedAssetPath(path);
            setOpenPaths((current) => current.includes(path) ? current : [...current, path]);
            setActivePath(path);
            setContent(initialContent);
            setSavedContent(initialContent);
        }
        return { path, created: true };
    };

    const createProjectDirectory = async (path: string): Promise<void> => {
        assertMutablePath(path);
        await storage.createDirectory(path);
        await refreshFiles();
        setSelectedAssetPath(path);
    };

    const renameProjectFile = async (source: string, destination: string) => {
        assertMutablePath(source);
        assertMutablePath(destination);
        if (source === activePath && dirty) await saveActiveFile();
        await storage.rename(source, destination);
        setOpenPaths((current) => current.map((path) => path === source ? destination : path));
        if (source === activePath) setActivePath(destination);
        if (source === selectedAssetPath) setSelectedAssetPath(destination);
        await refreshFiles();
        return { source, destination, renamed: true };
    };

    const removeProjectFile = async (path: string) => {
        assertMutablePath(path);
        const containsPath = (candidate: string): boolean =>
            candidate === path || candidate.startsWith(`${path}/`);
        await storage.remove(path);
        const wasActive = containsPath(activePath);
        const wasSelected = containsPath(selectedAssetPath);
        const entries = await refreshFiles();
        const remainingOpenPaths = openPaths.filter((candidate) => !containsPath(candidate));
        setOpenPaths(remainingOpenPaths);
        if (wasActive) {
            const next =
                remainingOpenPaths
                    .map((candidate) => entries.find((entry) => entry.path === candidate))
                    .find(
                        (entry) =>
                            entry && entry.kind !== "directory" && !entry.readonly,
                    ) ??
                entries.find(
                    (entry) => entry.kind !== "directory" && !entry.readonly,
                );
            if (next) {
                const nextContent = (await storage.read(next.path)) ?? "";
                setOpenPaths((current) => current.includes(next.path) ? current : [...current, next.path]);
                setActivePath(next.path);
                setContent(nextContent);
                setSavedContent(nextContent);
            } else {
                setActivePath("");
                setContent("");
                setSavedContent("");
            }
        }
        if (wasSelected) {
            const nextSelected =
                remainingOpenPaths.find((candidate) =>
                    entries.some((entry) => entry.path === candidate),
                ) ?? entries[0]?.path ?? "";
            setSelectedAssetPath(nextSelected);
        }
        return { path, removed: true };
    };

    const closeCodeFile = async (path: string): Promise<void> => {
        const index = openPaths.indexOf(path);
        if (index < 0) return;
        if (path === activePath && dirty) await saveActiveFile();
        const remaining = openPaths.filter((candidate) => candidate !== path);
        setOpenPaths(remaining);
        if (path !== activePath) return;

        const nextPath = remaining[Math.min(index, remaining.length - 1)];
        if (!nextPath) {
            setActivePath("");
            setContent("");
            setSavedContent("");
            setCursor({ line: 1, column: 1 });
            return;
        }
        const next = (await storage.read(nextPath)) ?? "";
        setActivePath(nextPath);
        setContent(next);
        setSavedContent(next);
        setCursor({ line: 1, column: 1 });
    };

    const projectSnapshot = async () => {
        if (dirty) await saveActiveFile();
        const entries = await storage.list();
        return Promise.all(
            entries.filter((entry) => entry.kind !== "directory").map(async (entry) => ({
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
        if (runtimeStopRef.current) await runtimeStopRef.current;
        const snapshot = await projectSnapshot();
        storeProfileArchive(null);
        await runtimeController.start(snapshot, force);
    };

    const restartRuntime = async (): Promise<void> => {
        if (runtimeStopRef.current) await runtimeStopRef.current;
        const snapshot = await projectSnapshot();
        storeProfileArchive(null);
        await runtimeController.restart(snapshot);
    };

    const showOperation = (
        next: Exclude<ProjectOperation, null>,
        targetPath = assetContextPath,
    ): void => {
        setOperation(next);
        setOperationError("");
        if (next === "new") {
            setOperationTargetPath("");
            setOperationPath("new.luau");
        } else if (next === "new-folder") {
            setOperationTargetPath("");
            setOperationPath("new_folder");
        } else {
            setOperationTargetPath(targetPath);
            setOperationPath(assetRelativePath(targetPath));
        }
    };

    const applyOperation = async (): Promise<void> => {
        try {
            const displayPath = operationPath.trim();
            const path = projectAssetPath(displayPath);
            if (operation === "new") {
                await createProjectFile(path, newFileContent(path), true);
                appendConsole("info", "project", `created ${displayPath}`);
            } else if (operation === "new-folder") {
                await createProjectDirectory(path);
                appendConsole("info", "project", `created folder ${displayPath}`);
            } else if (operation === "rename") {
                const source = operationTargetPath;
                await renameProjectFile(source, path);
                appendConsole("info", "project", `renamed ${assetRelativePath(source)} to ${displayPath}`);
            } else if (operation === "delete") {
                await removeProjectFile(operationTargetPath);
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
                    await loadOpenedProject();
                    appendConsole(
                        "info",
                        "project",
                        remembered.source === "bundled"
                            ? `opened bundled project ${remembered.name}`
                            : `restored local folder ${remembered.name}`,
                    );
                } else if (remembered?.permissionRequired) {
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
        const api = dockviewApiRef.current;
        if (!api) return;

        if (runtimeState === "starting") {
            if (api.activePanel?.id !== "game") {
                api.getPanel("game")?.api.setActive();
                runtimeFocusedGameRef.current = true;
            }
            return;
        }

        if (runtimeState === "stopped" && runtimeFocusedGameRef.current) {
            if (api.activePanel?.id === "game") api.getPanel("code")?.api.setActive();
            runtimeFocusedGameRef.current = false;
        }
    }, [runtimeState]);

    useEffect(() => {
        let refreshTimer: ReturnType<typeof setTimeout> | undefined;
        const unsubscribe = storage.subscribe((event) => {
            if (refreshTimer) clearTimeout(refreshTimer);
            refreshTimer = setTimeout(() => {
                void refreshFiles()
                    .then(async (entries) => {
                        await synchronizeProjectLanguageFiles(entries);
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

    const profilerAgentSource = async (): Promise<ProfilerAgentSource> => {
        if (runtimeStopRef.current) await runtimeStopRef.current;
        const snapshot = runtimeController.getSnapshot();
        if (snapshot.state === "running" && snapshot.session) {
            return {
                kind: "runtime",
                sessionId: snapshot.session.channelId,
                inspect: (provider, schema, payload) =>
                    runtimeController.inspect(provider, schema, payload),
            };
        }
        if (profileArchiveRef.current) {
            return { kind: "archive", archive: profileArchiveRef.current };
        }
        throw new Error("No live or retained profiler capture is available.");
    };

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
            await stopRuntime("stopped by command");
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
          "runtime.observe": async () => ({
              ...(await runtimeController.capture()),
            state: runtimeState,
        }),
        "runtime.key": async ({ code, action, durationMs }) =>
            runtimeController.key(code ?? "", action ?? "", durationMs),
        "runtime.pointer": async ({ x, y, action, button, durationMs }) =>
            runtimeController.pointerInput(
                x ?? Number.NaN,
                y ?? Number.NaN,
                action ?? "",
                button,
                durationMs,
            ),
        "runtime.wait": async ({ durationMs }) => {
            const duration = Math.max(0, Math.min(5000, durationMs ?? 0));
            await new Promise((resolve) => window.setTimeout(resolve, duration));
            const snapshot = runtimeController.getSnapshot();
            return {
                waitedMs: duration,
                state: snapshot.state,
                script: snapshot.script,
                frame: snapshot.frame,
            };
        },
        "runtime.clear_input": async () => runtimeController.clearInput(),
        "runtime.inspect": async ({ provider, schema, payload }) =>
            runtimeController.inspect(provider ?? "", schema ?? "", payload ?? {}),
        "profiler.summary": async () =>
            readProfilerSummary(await profilerAgentSource()),
        "profiler.frames": async ({ afterFrame, limit }) =>
            readProfilerFrames(await profilerAgentSource(), afterFrame, limit),
        "profiler.frame": async ({ frame }) =>
            readProfilerFrame(await profilerAgentSource(), frame ?? Number.NaN),
        "runtime.logs": async ({ limit }) => {
            const count = Math.max(1, Math.min(100, Math.floor(limit ?? 20)));
            return {
                logs: logs
                    .filter((entry) => entry.source === "game" || entry.source === "runtime")
                    .slice(-count)
                    .map(({ level, source, message, time }) => ({ level, source, message, time })),
            };
        },
    };

    const agentApi = useMemo<EditorAgentApi>(() => {
        return editorToolRegistry.createAgentApi({
            handler: (command) => handlersRef.current[command],
            requestId,
            onInvoke: (command) =>
                appendConsole("command", "agent", command || "invalid command"),
            onError: (message) => appendConsole("error", "agent", message),
        });
    }, [appendConsole]);

    const piAgent = useMemo(() => new EditorPiAgent(agentApi), [agentApi]);
    const modelGateway = useMemo(() => new EditorModelGateway(), []);

    useEffect(() => {
        if (!editorCapabilities.agent) return () => piAgent.dispose();
        window.entisiumEditor = {
            commands: agentApi,
            agents: { pi: piAgent },
        };
        // Compatibility aliases for integrations using the original split entry points.
        window.entisiumEditorAgent = agentApi;
        window.entisiumEditorPi = piAgent;
        return () => piAgent.dispose();
    }, [agentApi, piAgent]);

    useEffect(
        () => (editorCapabilities.agent ? connectEditorCommandBridge(agentApi) : undefined),
        [agentApi],
    );

    useEffect(() => {
        if (!editorCapabilities.agent) return;
        const updateStreaming = () => setAgentStreaming(piAgent.snapshot().streaming);
        updateStreaming();
        return piAgent.subscribeState(updateStreaming);
    }, [piAgent]);

    useEffect(() => {
        if (!editorCapabilities.agent) return;
        let cancelled = false;
        void modelGateway.connect(piAgent).then((state) => {
            if (!cancelled) setAgentGatewayState(state);
        });
        return () => {
            cancelled = true;
        };
    }, [modelGateway, piAgent]);

    useEffect(() => {
        if (!editorCapabilities.agent) return;
        let cancelled = false;
        void editorSettings
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

        if (addProfilerWorkbenchPanel(api)) {
            api.getPanel("profiler")?.group.api.setSize({ height: 280 });
        }

        dockviewLayoutListenerRef.current?.dispose();
        const panels = [
            ["code", "Code"],
            ["game", "Game"],
            ["project", "Assets"],
            ["inspector", "Inspector"],
            ["console", "Console"],
            ["profiler", "Profiler"],
        ] as const;
        const visiblePanels: readonly (readonly [string, string])[] = editorCapabilities.agent
            ? [["agent", "Agent"], ...panels]
            : panels;
        for (const [id, title] of visiblePanels) {
            api.getPanel(id)?.api.setTitle(title);
        }
        api.getPanel("code")?.api.setActive();
        localStorage.setItem(workbenchLayoutStorageKey, JSON.stringify(api.toJSON()));
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

    const workbenchPanels: Record<string, ReactNode> = {
        project: (
            <ContextMenu>
                <ContextMenuTrigger asChild>
                    <div
                        className="size-full"
                        onContextMenu={(event) => {
                            const target =
                                event.target instanceof Element
                                    ? event.target.closest<HTMLElement>("[data-asset-operation-path]")
                                    : null;
                            const path = target?.dataset.assetOperationPath ?? "";
                            setAssetContextPath(path);
                            if (path) setSelectedAssetPath(path);
                        }}
                    >
                        <ToolPanel>
                            <ScrollArea className="min-h-0 flex-1">
                            <nav className="min-h-full p-1.5" aria-label="Project files">
                                {!storage.isOpen ? (
                                    <PanelEmptyState>
                                        <Folder size={28} strokeWidth={1.3} />
                                        <span>Open a local project folder to begin.</span>
                                    </PanelEmptyState>
                                ) : (
                                    <FileTree
                                        files={files}
                                        selectedPath={selectedAssetPath}
                                        onSelect={(path) => void selectFile(path)}
                                        onInspect={setSelectedAssetPath}
                                    />
                                )}
                            </nav>
                            </ScrollArea>
                        </ToolPanel>
                    </div>
                </ContextMenuTrigger>
                <ContextMenuContent>
                    <ContextMenuItem disabled={!storage.isOpen} onSelect={() => showOperation("new")}>
                        <Plus />
                        New File…
                    </ContextMenuItem>
                    <ContextMenuItem disabled={!storage.isOpen} onSelect={() => showOperation("new-folder")}>
                        <FolderPlus />
                        New Folder…
                    </ContextMenuItem>
                    <ContextMenuItem
                        disabled={
                            !assetContextPath ||
                            files.find((entry) => entry.path === assetContextPath)?.kind === "directory"
                        }
                        onSelect={() => showOperation("rename")}
                    >
                        <Pencil />
                        Rename…
                    </ContextMenuItem>
                    <ContextMenuItem
                        className="text-destructive"
                        disabled={!assetContextPath}
                        onSelect={() => showOperation("delete")}
                    >
                        <Trash2 />
                        Delete…
                    </ContextMenuItem>
                    <ContextMenuSeparator />
                    <ContextMenuItem disabled={!storage.isOpen} onSelect={() => void refreshProjectFolder()}>
                        <RefreshCw />
                        Refresh
                    </ContextMenuItem>
                    <ContextMenuItem
                        disabled={!storage.isOpen}
                        onSelect={() =>
                            void showProjectSettings().catch((error) =>
                                appendConsole("error", "project", errorMessage(error)),
                            )
                        }
                    >
                        <Settings2 />
                        Project Settings…
                    </ContextMenuItem>
                </ContextMenuContent>
            </ContextMenu>
        ),
        code: (
            <ToolPanel className="editor-panel">
                <CodeFileTabs
                    paths={openPaths}
                    activePath={activePath}
                    dirty={dirty}
                    onSelect={(path) => void selectFile(path)}
                    onClose={(path) => void closeCodeFile(path)}
                />
                {activePath ? (
                    <CodeEditor
                        key={activePath}
                        path={activePath}
                        projectRootUri={storage.rootUri}
                        value={content}
                        readOnly={!canEdit}
                        onChange={setContent}
                        onCursorChange={(line, column) => setCursor({ line, column })}
                        onLanguageClientStatus={setLuauLspStatus}
                    />
                ) : (
                    <PanelEmptyState>Select a file to edit.</PanelEmptyState>
                )}
                <PanelStatus className="justify-end">
                    <span>{dirty ? "● Unsaved" : "Saved"}</span>
                    <span>Ln {cursor.line}, Col {cursor.column}</span>
                    <span>{languageForPath(activePath)}</span>
                    {editorCapabilities.luauLsp && activePath.endsWith(".luau") && (
                        <span>Luau LSP: {luauLspStatus}</span>
                    )}
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
                <ScrollArea viewportRef={consoleRef} className="min-h-0 flex-1 bg-[#191919]">
                    <div className="min-h-full px-2.5 py-1.5 font-mono text-[12px] leading-[1.55]" aria-live="polite">
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
        profiler: (
            <ProfilerPanel
                runtimeState={runtimeState}
                sessionId={runtimeSession?.channelId ?? null}
                inspect={inspectRuntime}
                archive={profileArchive}
                finalizing={profileFinalizing}
                finalizeProgress={profileFinalizeProgress}
                onClearArchive={() => storeProfileArchive(null)}
            />
        ),
        ...(editorCapabilities.agent
            ? {
                  agent: (
                      <ToolPanel className="min-h-0 bg-[#191919]">
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
              }
            : {}),
        inspector: (
            <ToolPanel>
                <ScrollArea className="min-h-0 flex-1">
                    <ResourceInspector
                        inspection={assetInspection}
                        loading={assetInspectionLoading}
                        error={assetInspectionError}
                        previewUrl={assetPreviewUrl}
                        onOpen={() => {
                            if (selectedAssetPath) void selectFile(selectedAssetPath);
                        }}
                        onRename={() => showOperation("rename", selectedAssetPath)}
                        onDelete={() => showOperation("delete", selectedAssetPath)}
                    />
                </ScrollArea>
            </ToolPanel>
        ),
    };

    return (
        <TooltipProvider delayDuration={450}>
            <div className="flex size-full flex-col overflow-clip bg-background">
                <EditorTopbar
                    projectOpen={storage.isOpen}
                    canSave={canEdit}
                    runtimeState={runtimeState}
                    runtimeBusy={profileFinalizing}
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
                    settingsAvailable={editorCapabilities.agent}
                    onResetWorkbench={resetWorkbenchLayout}
                    onPlay={() => {
                        void playRuntime().catch((error) =>
                            appendConsole("error", "runtime", errorMessage(error)),
                        );
                    }}
                    onStop={() => {
                        void stopRuntime().catch((error) =>
                            appendConsole("error", "runtime", errorMessage(error)),
                        );
                    }}
                    onRestart={() => {
                        void restartRuntime().catch((error) =>
                            appendConsole("error", "runtime", errorMessage(error)),
                        );
                    }}
                />

                <main className="min-h-0 flex-1 bg-[#101010] p-1">
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

                {editorCapabilities.agent && (
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
                )}
            </div>
        </TooltipProvider>
    );
}
