import Editor, { loader, type OnMount } from "@monaco-editor/react";
import * as monaco from "monaco-editor/editor/editor.api";
import "monaco-editor/languages/definitions/cpp/register";
import "monaco-editor/languages/definitions/lua/register";
import "monaco-editor/languages/definitions/yaml/register";
import "monaco-editor/language/json/monaco.contribution";
import editorWorker from "monaco-editor/editor/editor.worker?worker";
import jsonWorker from "monaco-editor/language/json/json.worker?worker";
import { useCallback } from "react";
import {
    enableLuauTextmate,
    luauEditorTheme,
    registerLuauLanguage,
} from "../languages/luau";

self.MonacoEnvironment = {
    getWorker(_moduleId: string, label: string) {
        return label === "json" ? new jsonWorker() : new editorWorker();
    },
};
loader.config({ monaco });
registerLuauLanguage(monaco);
void enableLuauTextmate(monaco).catch((error: unknown) => {
    console.error("[entisium editor] failed to enable Luau TextMate grammar", error);
});

interface CodeEditorProps {
    path: string;
    value: string;
    readOnly: boolean;
    onChange(value: string): void;
    onCursorChange(line: number, column: number): void;
}

function languageForPath(path: string): string {
    if (path.endsWith(".luau")) return "luau";
    if (path.endsWith(".lua")) return "lua";
    if (path.endsWith(".json")) return "json";
    if (path.endsWith(".cpp") || path.endsWith(".cc")) return "cpp";
    if (/\.(?:h|hpp|hxx)$/.test(path)) return "cpp";
    return "plaintext";
}

export function CodeEditor({
    path,
    value,
    readOnly,
    onChange,
    onCursorChange,
}: CodeEditorProps) {
    const onMount = useCallback<OnMount>(
        (editor) => {
            const position = editor.getPosition();
            if (position) onCursorChange(position.lineNumber, position.column);
            editor.onDidChangeCursorPosition(({ position: next }) => {
                onCursorChange(next.lineNumber, next.column);
            });
        },
        [onCursorChange],
    );

    return (
        <div id="source-editor" className="min-h-0 flex-1 bg-[#0c1016]" data-disabled={String(readOnly)}>
            <Editor
                path={path ? `file:///project/${path}` : "inmemory://empty"}
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
                    fontFamily: '"Cascadia Code", "SFMono-Regular", Consolas, monospace',
                    fontSize: 14,
                    guides: {
                        bracketPairs: false,
                        bracketPairsHorizontal: false,
                        highlightActiveBracketPair: false,
                    },
                    lineHeight: 22,
                    minimap: { enabled: false },
                    padding: { top: 10, bottom: 10 },
                    renderLineHighlight: "line",
                    scrollBeyondLastLine: false,
                    smoothScrolling: true,
                    tabSize: 4,
                    wordWrap: "off",
                }}
            />
        </div>
    );
}
