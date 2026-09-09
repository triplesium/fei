/// <reference types="vite/client" />

declare module "@codingame/monaco-vscode-editor-api/esm/vs/editor/editor.worker?worker" {
    const EditorWorker: new () => Worker;
    export default EditorWorker;
}

declare module "@codingame/monaco-vscode-standalone-json-language-features/worker?worker" {
    const JsonWorker: new () => Worker;
    export default JsonWorker;
}
