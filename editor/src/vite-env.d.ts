/// <reference types="vite/client" />

declare module "monaco-editor/editor/editor.worker?worker" {
    const EditorWorker: new () => Worker;
    export default EditorWorker;
}

declare module "monaco-editor/language/json/json.worker?worker" {
    const JsonWorker: new () => Worker;
    export default JsonWorker;
}
