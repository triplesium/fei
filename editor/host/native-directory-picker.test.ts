import { describe, expect, it } from "vitest";
import { projectDirectoryFromArguments } from "./native-directory-picker.js";

describe("projectDirectoryFromArguments", () => {
    it("prefers command line project paths over the environment", () => {
        expect(
            projectDirectoryFromArguments(["--project", "D:\\Projects\\game"], {
                FEI_EDITOR_PROJECT_DIR: "D:\\Projects\\fallback",
            }),
        ).toBe("D:\\Projects\\game");
        expect(projectDirectoryFromArguments(["--project=/tmp/game"], {})).toBe("/tmp/game");
    });

    it("falls back to FEI_EDITOR_PROJECT_DIR", () => {
        expect(
            projectDirectoryFromArguments([], { FEI_EDITOR_PROJECT_DIR: "/tmp/fallback" }),
        ).toBe("/tmp/fallback");
    });
});
