import {
    EditorsOrder,
    getService,
    IEditorService,
    IInstantiationService,
    IMarkdownRendererService,
} from "@codingame/monaco-vscode-api";
import {
    createModelReference,
    type ITextFileEditorModel,
} from "@codingame/monaco-vscode-api/monaco";
import { renderEditorPart } from "@codingame/monaco-vscode-views-service-override";
import { EditorMarkdownCodeBlockRenderer } from "@codingame/monaco-vscode-api/vscode/vs/editor/browser/widget/markdownRenderer/browser/editorMarkdownCodeBlockRenderer";
import * as vscodeMonaco from "@codingame/monaco-vscode-editor-api";
import "@codingame/monaco-vscode-standalone-languages/cpp/cpp.contribution.js";
import "@codingame/monaco-vscode-standalone-languages/lua/lua.contribution.js";
import "@codingame/monaco-vscode-standalone-languages/yaml/yaml.contribution.js";
import "@codingame/monaco-vscode-standalone-json-language-features";
import editorWorker from "@codingame/monaco-vscode-editor-api/esm/vs/editor/editor.worker?worker";
import jsonWorker from "@codingame/monaco-vscode-standalone-json-language-features/worker?worker";
import { MonacoVscodeApiWrapper } from "monaco-languageclient/vscodeApiWrapper";
import { registerLuauExtension, registerLuauLanguage } from "../languages/luau";
import { registerMaterialIconTheme } from "./material-icon-theme";
import { vscodeProjectPath, vscodeProjectResource } from "./vscode-project-files";

const host = document.createElement("div");
host.className = "vscode-editor-host";
const editorPart = document.createElement("div");
editorPart.className = "vscode-editor-part";
host.append(editorPart);

let startPromise: Promise<void> | undefined;
let resolveHostMounted: (() => void) | undefined;
const hostMounted = new Promise<void>((resolve) => {
    resolveHostMounted = resolve;
});

function configureWorkers(): void {
    self.MonacoEnvironment = {
        getWorker(_moduleId: string, label: string) {
            return label === "json" ? new jsonWorker() : new editorWorker();
        },
    };
}

function startVscodeApi(): Promise<void> {
    if (startPromise) return startPromise;

    configureWorkers();
    registerLuauExtension();
    registerMaterialIconTheme();
    const api = new MonacoVscodeApiWrapper({
        $type: "extended",
        viewsConfig: {
            $type: "ViewsService",
            htmlContainer: host,
        },
        monacoWorkerFactory: configureWorkers,
        userConfiguration: {
            json: JSON.stringify({
                "workbench.colorTheme": "Default Dark Modern",
                "workbench.iconTheme": "material-icon-theme",
                "workbench.editor.showTabs": "multiple",
                "workbench.editor.showIcons": true,
                "workbench.editor.enablePreview": true,
                "workbench.editor.tabSizing": "fit",
                "workbench.editor.wrapTabs": false,
                "window.density.editorTabHeight": "default",
                "editor.fontFamily": "Cascadia Code, SFMono-Regular, Consolas, monospace",
                "editor.fontSize": 14,
                "editor.lineHeight": 22,
                "editor.minimap.enabled": false,
                "editor.scrollBeyondLastLine": false,
                "editor.smoothScrolling": true,
                "editor.tabSize": 4,
                "editor.bracketPairColorization.enabled": false,
                "editor.guides.bracketPairs": false,
                "luau-lsp.platform.type": "standard",
                "luau-lsp.sourcemap.enabled": false,
                "luau-lsp.types.roblox": false,
                "luau-lsp.fflags.override": {
                    LuauExportValueSyntax: "true",
                },
            }),
        },
        advanced: {
            enableExtHostWorker: false,
            loadExtensionServices: false,
            loadThemes: true,
        },
    });

    startPromise = api.start({ caller: "Entisium Editor" }).then(async () => {
        (await getService(IMarkdownRendererService)).setDefaultCodeBlockRenderer(
            (await getService(IInstantiationService)).createInstance(
                EditorMarkdownCodeBlockRenderer,
            ),
        );
        const monaco = vscodeMonaco as unknown as typeof import("monaco-editor");
        registerLuauLanguage(monaco, { registerTheme: false });
        renderEditorPart(editorPart);
    });
    return startPromise;
}

export async function waitForVscodeEditorHost(): Promise<void> {
    await hostMounted;
    await startVscodeApi();
}

export function mountVscodeEditorHost(container: HTMLElement): void {
    container.replaceChildren(host);
    resolveHostMounted?.();
    resolveHostMounted = undefined;
}

async function getEditorService() {
    await waitForVscodeEditorHost();
    return getService(IEditorService);
}

export async function openVscodeProjectFile(path: string, pinned = true): Promise<void> {
    const resource = vscodeProjectResource(path);
    const service = await getEditorService();
    await service.openEditor({
        resource,
        options: {
            forceReload: true,
            pinned,
            revealIfOpened: true,
        },
    });
}

export async function saveAllVscodeProjectFiles(): Promise<void> {
    if (!startPromise) return;
    const result = await (await getEditorService()).saveAll();
    if (!result.success) throw new Error("One or more editor files could not be saved.");
}

export async function closeAllVscodeProjectFiles(): Promise<void> {
    if (!startPromise) return;
    const service = await getEditorService();
    await service.closeEditors(service.getEditors(EditorsOrder.SEQUENTIAL));
}

export async function openedVscodeProjectPathsUnder(path: string): Promise<string[]> {
    const service = await getEditorService();
    return service
        .getEditors(EditorsOrder.SEQUENTIAL)
        .map(({ editor }) => vscodeProjectPath(editor.resource))
        .filter(
            (candidate): candidate is string =>
                candidate === path || candidate.startsWith(`${path}/`),
        );
}

export async function closeVscodeProjectPaths(paths: readonly string[]): Promise<void> {
    if (paths.length === 0) return;
    const targets = new Set(paths);
    const service = await getEditorService();
    await service.closeEditors(
        service
            .getEditors(EditorsOrder.SEQUENTIAL)
            .filter(({ editor }) => targets.has(vscodeProjectPath(editor.resource))),
    );
}

export async function readVscodeProjectFile(path: string): Promise<string> {
    const reference = await createModelReference(vscodeProjectResource(path));
    try {
        const model = reference.object.textEditorModel;
        if (!model) throw new Error(`VS Code did not resolve a text model for ${path}`);
        return model.getValue();
    } finally {
        reference.dispose();
    }
}

export async function writeVscodeProjectFile(path: string, content: string): Promise<void> {
    const reference = await createModelReference(vscodeProjectResource(path));
    try {
        const fileModel: ITextFileEditorModel = reference.object;
        const model = fileModel.textEditorModel;
        if (!model) throw new Error(`VS Code did not resolve a text model for ${path}`);
        if (model.getValue() !== content) model.setValue(content);
        if (!(await fileModel.save())) throw new Error(`VS Code could not save ${path}`);
    } finally {
        reference.dispose();
    }
}

export async function subscribeVscodeActiveProjectFile(
    listener: (path: string) => void,
): Promise<{ dispose(): void }> {
    const service = await getEditorService();
    const publish = () => listener(vscodeProjectPath(service.activeEditor?.resource));
    publish();
    return service.onDidActiveEditorChange(publish);
}
