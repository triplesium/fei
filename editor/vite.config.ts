import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
    base: "./",
    plugins: [react()],
    server: {
        proxy: {
            "/api": "http://127.0.0.1:3100",
            "/sample-browser-project": "http://127.0.0.1:3100",
        },
    },
    build: {
        outDir: "dist",
        emptyOutDir: true,
    },
});
