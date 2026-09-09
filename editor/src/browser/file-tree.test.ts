import { describe, expect, it } from "vitest";

import { buildFileTree } from "./file-tree";
import type { ProjectFileEntry } from "./types";

describe("buildFileTree", () => {
    it("places directories before files at every level", () => {
        const files: ProjectFileEntry[] = [
            { path: "assets/zeta.luau", kind: "text", readonly: false },
            { path: "assets/images", kind: "directory", readonly: false },
            { path: "assets/alpha.luau", kind: "text", readonly: false },
            { path: "assets/scripts/main.luau", kind: "text", readonly: false },
            { path: "assets/images/generated", kind: "directory", readonly: false },
            { path: "assets/images/raw/source.png", kind: "binary", readonly: true },
            { path: "assets/images/zeta.png", kind: "binary", readonly: true },
            { path: "assets/images/alpha.png", kind: "binary", readonly: true },
        ];

        const tree = buildFileTree(files);

        expect(tree.map((node) => node.name)).toEqual([
            "images",
            "scripts",
            "alpha.luau",
            "zeta.luau",
        ]);
        expect(tree[0].children.map((node) => node.name)).toEqual([
            "generated",
            "raw",
            "alpha.png",
            "zeta.png",
        ]);
    });
});
