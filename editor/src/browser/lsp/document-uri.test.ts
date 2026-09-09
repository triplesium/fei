import { describe, expect, it } from "vitest";
import { documentUri, planMemoryDirectories } from "./document-uri";

describe("documentUri", () => {
    it("joins and encodes project-relative path segments", () => {
        expect(
            documentUri(
                "file:///D:/Projects/sample",
                "assets/my scripts/gameplay#1.luau",
            ),
        ).toBe(
            "file:///D:/Projects/sample/assets/my%20scripts/gameplay%231.luau",
        );
    });

    it("uses the fallback project root when the host has no root URI", () => {
        expect(documentUri("", "my scripts/gameplay#1.luau")).toBe(
            "file:///project/my%20scripts/gameplay%231.luau",
        );
    });
});

describe("planMemoryDirectories", () => {
    it("seeds a Windows drive before its child directories", () => {
        expect(planMemoryDirectories("/d:/Projects/gameplay.luau")).toEqual({
            driveRoot: "/d:",
            directories: ["/d:/Projects"],
        });
    });

    it("creates ordinary POSIX parent directories in order", () => {
        expect(planMemoryDirectories("/project/scripts/gameplay.luau")).toEqual({
            driveRoot: undefined,
            directories: ["/project", "/project/scripts"],
        });
    });
});
