import Editor, { loader, type OnMount } from "@monaco-editor/react";
import {
    createModelReference,
    type IReference,
    type ITextFileEditorModel,
} from "@codingame/monaco-vscode-api/monaco";
import { URI } from "@codingame/monaco-vscode-api/vscode/vs/base/common/uri";
import {
    InMemoryFileSystemProvider,
    registerFileSystemOverlay,
} from "@codingame/monaco-vscode-files-service-override";
import * as vscodeMonaco from "@codingame/monaco-vscode-editor-api";
import "@codingame/monaco-vscode-standalone-languages/cpp/cpp.contribution.js";
import "@codingame/monaco-vscode-standalone-languages/lua/lua.contribution.js";
import "@codingame/monaco-vscode-standalone-languages/yaml/yaml.contribution.js";
import "@codingame/monaco-vscode-standalone-json-language-features";
import editorWorker from "@codingame/monaco-vscode-editor-api/esm/vs/editor/editor.worker?worker";
import jsonWorker from "@codingame/monaco-vscode-standalone-json-language-features/worker?worker";
import { useCallback, useEffect, useRef, useState } from "react";
import { editorCapabilities } from "@editor-platform/capabilities";
import {
    prepareLuauLanguageClient,
    startLuauLanguageClient,
    subscribeLuauLanguageClient,
    type LuauLanguageClientStatus,
} from "@editor-platform/luau-language-client";
import {
    enableLuauTextmate,
    luauEditorTheme,
    registerLuauLanguage,
} from "../languages/luau";
import { documentUri, planMemoryDirectories } from "../lsp/document-uri";

await prepareLuauLanguageClient();

self.MonacoEnvironment = {
    getWorker(_moduleId: string, label: string) {
        return label === "json" ? new jsonWorker() : new editorWorker();
    },
};
const editorMonaco = vscodeMonaco as unknown as typeof import("monaco-editor");
loader.config({ monaco: editorMonaco });
registerLuauLanguage(editorMonaco);
void enableLuauTextmate(editorMonaco).catch((error: unknown) => {
    console.error("[entisium editor] failed to enable Luau TextMate grammar", error);
});

interface CodeEditorProps {
    path: string;
    projectRootUri: string;
    value: string;
    readOnly: boolean;
    onChange(value: string): void;
    onCursorChange(line: number, column: number): void;
    onLanguageClientStatus(status: LuauLanguageClientStatus): void;
}

function languageForPath(path: string): string {
    if (path.endsWith(".luau")) return "luau";
    if (path.endsWith(".lua")) return "lua";
    if (path.endsWith(".json")) return "json";
    if (path.endsWith(".cpp") || path.endsWith(".cc")) return "cpp";
    if (/\.(?:h|hpp|hxx)$/.test(path)) return "cpp";
    return "plaintext";
}

async function createVscodeModel(uri: string, language: string, content: string) {
    // Round-tripping normalizes Windows drive letters. The registered file
    // provider stores path segments case-sensitively while VS Code file URIs
    // canonicalize the drive letter to lowercase.
    const resource = URI.parse(URI.parse(uri).toString());
    const provider = new InMemoryFileSystemProvider();
    let overlayRegistration: { dispose(): void } | undefined;

    try {
        const directoryPlan = planMemoryDirectories(resource.path);
        if (directoryPlan.driveRoot) {
            // A Windows drive root is its own dirname for file URIs, so seed it
            // through a non-file URI before creating the remaining directories.
            await provider.mkdir(
                resource.with({
                    scheme: "entisium-memory",
                    path: directoryPlan.driveRoot,
                }),
            );
        }
        for (const directory of directoryPlan.directories) {
            await provider.mkdir(resource.with({ path: directory }));
        }
        await provider.writeFile(resource, new TextEncoder().encode(content), {
            atomic: false,
            create: true,
            overwrite: true,
            unlock: false,
        });
        overlayRegistration = registerFileSystemOverlay(1, provider);

        const reference = await createModelReference(resource);
        reference.object.setLanguageId(language);
        const model = reference.object.textEditorModel;
        if (!model) {
            reference.dispose();
            throw new Error("VS Code model reference did not resolve a text model");
        }
        return {
            reference,
            model,
            provider,
            overlayRegistration,
        };
    } catch (error) {
        overlayRegistration?.dispose();
        provider.dispose();
        throw error;
    }
}

interface VscodeModelAttachment {
    reference: IReference<ITextFileEditorModel>;
    provider: InMemoryFileSystemProvider;
    overlayRegistration: { dispose(): void };
}

function disposeVscodeModel(attachment: VscodeModelAttachment | undefined): void {
    if (!attachment) return;
    attachment.reference.dispose();
    attachment.overlayRegistration.dispose();
    attachment.provider.dispose();
}

export function CodeEditor({
    path,
    projectRootUri,
    value,
    readOnly,
    onChange,
    onCursorChange,
    onLanguageClientStatus,
}: CodeEditorProps) {
    const modelAttachment = useRef<VscodeModelAttachment | undefined>(undefined);
    const modelGeneration = useRef(0);
    const [overflowWidgetsDomNode, setOverflowWidgetsDomNode] = useState<HTMLDivElement>();

    useEffect(() => {
        const node = document.createElement("div");
        node.className = "monaco-editor vs-dark editor-overflow-widgets";
        document.body.append(node);
        setOverflowWidgetsDomNode(node);
        return () => node.remove();
    }, []);

    useEffect(
        () => subscribeLuauLanguageClient(onLanguageClientStatus),
        [onLanguageClientStatus],
    );
    useEffect(() => {
        if (!editorCapabilities.luauLsp || !path.endsWith(".luau")) return;
        void startLuauLanguageClient(projectRootUri);
    }, [path, projectRootUri]);
    useEffect(
        () => () => {
            modelGeneration.current += 1;
            disposeVscodeModel(modelAttachment.current);
            modelAttachment.current = undefined;
        },
        [],
    );

    const onMount = useCallback<OnMount>(
        (editor) => {
            const transientModel = editor.getModel();
            const generation = ++modelGeneration.current;
            void createVscodeModel(
                documentUri(projectRootUri, path),
                languageForPath(path),
                transientModel?.getValue() ?? value,
            )
                .then((attachment) => {
                    if (generation !== modelGeneration.current) {
                        disposeVscodeModel(attachment);
                        return;
                    }

                    const { model } = attachment;
                    const latestValue = transientModel?.getValue();
                    if (latestValue !== undefined && latestValue !== model.getValue()) {
                        model.setValue(latestValue);
                    }

                    disposeVscodeModel(modelAttachment.current);
                    modelAttachment.current = attachment;
                    editor.setModel(model);
                    if (transientModel && transientModel !== model) transientModel.dispose();
                })
                .catch((error: unknown) => {
                    console.error(
                        `[entisium editor] failed to attach VS Code model for ${path}`,
                        error,
                    );
                });

            const position = editor.getPosition();
            if (position) onCursorChange(position.lineNumber, position.column);
            editor.onDidChangeCursorPosition(({ position: next }) => {
                onCursorChange(next.lineNumber, next.column);
            });
        },
        [onCursorChange, path, projectRootUri, value],
    );

    return (
        <div id="source-editor" className="min-h-0 flex-1 bg-[#0c1016]" data-disabled={String(readOnly)}>
            {overflowWidgetsDomNode ? (
                <Editor
                    language={languageForPath(path)}
                    value={value}
                    theme={luauEditorTheme}
                    onChange={(next) => onChange(next ?? "")}
                    onMount={onMount}
                    keepCurrentModel
                    options={{
                        readOnly,
                        automaticLayout: true,
                        bracketPairColorization: { enabled: false },
                        // Dockview clips each panel at its bounds. Keep Monaco's overflow
                        // widgets in the document layer so hovers can extend across panels.
                        fixedOverflowWidgets: true,
                        fontFamily:
                            '"Cascadia Code", "SFMono-Regular", Consolas, monospace',
                        fontSize: 14,
                        guides: {
                            bracketPairs: false,
                            bracketPairsHorizontal: false,
                            highlightActiveBracketPair: false,
                        },
                        lineHeight: 22,
                        minimap: { enabled: false },
                        overflowWidgetsDomNode,
                        padding: { top: 10, bottom: 10 },
                        renderLineHighlight: "line",
                        scrollBeyondLastLine: false,
                        smoothScrolling: true,
                        tabSize: 4,
                        wordWrap: "off",
                    }}
                />
            ) : null}
        </div>
    );
}
