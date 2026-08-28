import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import path from "node:path";

export default defineConfig(({ mode }) => {
    const platform = mode === "demo" ? "demo" : "host";
    return {
        base: "./",
        plugins: [react(), tailwindcss()],
        resolve: {
            alias: {
                "@editor-platform": path.resolve(import.meta.dirname, "src", "platform", platform),
                "@": path.resolve(import.meta.dirname, "src"),
            },
        },
        server: {
            headers: {
                "Cross-Origin-Embedder-Policy": "require-corp",
                "Cross-Origin-Opener-Policy": "same-origin",
            },
            proxy: {
                "/api": "http://127.0.0.1:3100",
                "/runtime": "http://127.0.0.1:3100",
            },
        },
        build: {
            outDir: path.resolve(import.meta.dirname, "dist", platform),
            emptyOutDir: true,
        },
    };
});
