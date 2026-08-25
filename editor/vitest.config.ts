import { defineConfig } from "vitest/config";
import path from "node:path";

export default defineConfig({
    resolve: {
        alias: {
            "@editor-platform": path.resolve(import.meta.dirname, "src", "platform", "host"),
        },
    },
    test: {
        include: ["src/**/*.test.ts", "host/**/*.test.ts"],
    },
});
