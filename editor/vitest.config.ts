import { defineConfig } from "vitest/config";
import path from "node:path";

export default defineConfig({
    resolve: {
        conditions: ["development"],
        alias: {
            "@editor-platform": path.resolve(import.meta.dirname, "src", "browser", "platform", "host"),
        },
    },
    test: {
        include: ["src/**/*.test.ts"],
    },
});
