import { describe, expect, it } from "vitest";
import type { ProfileEntry } from "../runtime/profiling";
import {
    applyProfileSymbolManifest,
    type ProfileSymbolManifest,
} from "./profile-symbols";

describe("profiling symbol manifests", () => {
    it("resolves Wasm function indices without changing the system identity", () => {
        const entry: ProfileEntry = {
            scheduleId: 7,
            systemId: 19,
            scheduleName: "Update",
            symbol: {
                kind: "wasm-function-index",
                moduleId: "wasm:abc",
                id: 42,
            },
            name: "system#19",
            file: "<unknown>",
            functionName: "<unknown>",
            line: 0,
            count: 2,
            totalMs: 3,
            selfMs: 2,
            meanMs: 1.5,
            selfMeanMs: 1,
            minMs: 1,
            maxMs: 2,
        };
        const manifest: ProfileSymbolManifest = {
            schema: "entisium.profile-symbols.v1",
            module_id: "wasm:abc",
            kind: "wasm-function-index",
            symbols: {
                "42": {
                    function: "void game::PlayerSystem<game::Position>::update(ets::World&)",
                },
            },
        };

        expect(applyProfileSymbolManifest(entry, manifest)).toMatchObject({
            systemId: 19,
            name: "update",
            functionName: "void game::PlayerSystem<game::Position>::update(ets::World&)",
            count: 2,
        });
    });

    it("leaves entries untouched when the build identifier does not match", () => {
        const entry = {
            scheduleId: 0,
            systemId: 1,
            scheduleName: "",
            symbol: { kind: "wasm-function-index" as const, moduleId: "wasm:a", id: 1 },
            name: "fallback",
            file: "<unknown>",
            functionName: "<unknown>",
            line: 0,
            count: 1,
            totalMs: 1,
            selfMs: 1,
            meanMs: 1,
            selfMeanMs: 1,
            minMs: 1,
            maxMs: 1,
        };
        const manifest: ProfileSymbolManifest = {
            schema: "entisium.profile-symbols.v1",
            module_id: "wasm:b",
            kind: "wasm-function-index",
            symbols: { "1": { function: "other()" } },
        };

        expect(applyProfileSymbolManifest(entry, manifest)).toBe(entry);
    });
});
