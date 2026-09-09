import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import path from "node:path";
import { createRequire } from "node:module";

const materialIconRoot = path.dirname(createRequire(import.meta.url).resolve("material-icon-theme/package.json"));

export default defineConfig(({ mode }) => {
    const platform = mode === "demo" ? "demo" : "host";
    return {
        base: "./",
        plugins: [react(), tailwindcss()],
        resolve: {
            alias: {
                "@material-icon-theme": materialIconRoot,
                "@editor-platform": path.resolve(import.meta.dirname, "src", "browser", "platform", platform),
                "@": path.resolve(import.meta.dirname, "src", "browser"),
            },
        },
        server: {
            headers: {
                "Cross-Origin-Embedder-Policy": "require-corp",
                "Cross-Origin-Opener-Policy": "same-origin",
            },
            proxy: {
                "/api": {
                    target: "http://127.0.0.1:3100",
                    ws: true,
                },
                "/runtime": "http://127.0.0.1:3100",
                "/profile-symbols": "http://127.0.0.1:3100",
            },
        },
        preview: {
            headers: {
                "Cross-Origin-Embedder-Policy": "require-corp",
                "Cross-Origin-Opener-Policy": "same-origin",
            },
        },
        build: {
            outDir: path.resolve(import.meta.dirname, "dist", platform === "demo" ? "demo" : "browser"),
            emptyOutDir: true,
        },
    };
});
