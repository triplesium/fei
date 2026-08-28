import type {
    ProfileEntry,
    ProfileFrameDetails,
    ProfileSummary,
} from "../runtime/profiling";
import { editorHost } from "./editor-host-client";

export interface ProfileSymbolManifestEntry {
    function: string;
    file?: string;
    line?: number;
}

export interface ProfileSymbolManifest {
    schema: "entisium.profile-symbols.v1";
    module_id: string;
    kind: "wasm-function-index";
    symbols: Record<string, ProfileSymbolManifestEntry>;
}

interface ProfileSymbolCache {
    manifest: ProfileSymbolManifest | null;
    knownIds: Set<string>;
    queue: Promise<void>;
    unavailable: boolean;
}

const manifests = new Map<string, ProfileSymbolCache>();
const maximumSymbolsPerRequest = 512;

function isManifest(value: unknown, moduleId: string): value is ProfileSymbolManifest {
    if (!value || typeof value !== "object" || Array.isArray(value)) return false;
    const manifest = value as Partial<ProfileSymbolManifest>;
    return (
        manifest.schema === "entisium.profile-symbols.v1" &&
        manifest.module_id === moduleId &&
        manifest.kind === "wasm-function-index" &&
        Boolean(manifest.symbols) &&
        typeof manifest.symbols === "object" &&
        !Array.isArray(manifest.symbols)
    );
}

async function loadManifest(
    moduleId: string,
    symbolIds: readonly string[],
): Promise<ProfileSymbolManifest | null> {
    let cache = manifests.get(moduleId);
    if (!cache) {
        cache = {
            manifest: null,
            knownIds: new Set(),
            queue: Promise.resolve(),
            unavailable: false,
        };
        manifests.set(moduleId, cache);
    }

    cache.queue = cache.queue
        .then(async () => {
            if (cache!.unavailable) return;
            const missingIds = symbolIds.filter((id) => !cache!.knownIds.has(id));
            for (let offset = 0; offset < missingIds.length; offset += maximumSymbolsPerRequest) {
                const ids = missingIds.slice(offset, offset + maximumSymbolsPerRequest);
                const value = await editorHost.json<unknown>(
                    `/api/v1/profile-symbols?module=${encodeURIComponent(moduleId)}&ids=${ids.join(",")}`,
                );
                if (!isManifest(value, moduleId)) {
                    throw new Error("Invalid profiling symbol manifest.");
                }
                if (!cache!.manifest) {
                    cache!.manifest = { ...value, symbols: {} };
                }
                Object.assign(cache!.manifest.symbols, value.symbols);
                ids.forEach((id) => cache!.knownIds.add(id));
            }
        })
        .catch(() => {
            cache!.unavailable = true;
        });
    await cache.queue;
    return cache.unavailable ? null : cache.manifest;
}

function leafFunctionName(functionName: string): string {
    let depth = 0;
    let withoutTemplates = "";
    for (const character of functionName) {
        if (character === "<") {
            depth += 1;
        } else if (character === ">" && depth > 0) {
            depth -= 1;
        } else if (depth === 0) {
            withoutTemplates += character;
        }
    }
    const signature = withoutTemplates.split("(", 1)[0] ?? withoutTemplates;
    const scope = signature.lastIndexOf("::");
    return (scope >= 0 ? signature.slice(scope + 2) : signature).trim() || functionName;
}

export function applyProfileSymbolManifest(
    entry: ProfileEntry,
    manifest: ProfileSymbolManifest,
): ProfileEntry {
    if (
        entry.symbol?.kind !== manifest.kind ||
        entry.symbol.moduleId !== manifest.module_id
    ) {
        return entry;
    }
    const symbol = manifest.symbols[String(entry.symbol.id)];
    if (!symbol || typeof symbol.function !== "string" || !symbol.function) return entry;
    return {
        ...entry,
        name: leafFunctionName(symbol.function),
        functionName: symbol.function,
        file: typeof symbol.file === "string" ? symbol.file : entry.file,
        line: typeof symbol.line === "number" ? symbol.line : entry.line,
    };
}

async function resolveEntries(entries: ProfileEntry[]): Promise<ProfileEntry[]> {
    const symbolIdsByModule = new Map<string, Set<string>>();
    for (const entry of entries) {
        if (entry.symbol?.kind !== "wasm-function-index") continue;
        let ids = symbolIdsByModule.get(entry.symbol.moduleId);
        if (!ids) {
            ids = new Set();
            symbolIdsByModule.set(entry.symbol.moduleId, ids);
        }
        ids.add(String(entry.symbol.id));
    }
    const loaded = await Promise.all(
        [...symbolIdsByModule].map(
            async ([moduleId, ids]) =>
                [moduleId, await loadManifest(moduleId, [...ids])] as const,
        ),
    );
    const byModule = new Map(loaded);
    return entries.map((entry) => {
        const moduleId = entry.symbol?.moduleId;
        const manifest = moduleId ? byModule.get(moduleId) : null;
        return manifest ? applyProfileSymbolManifest(entry, manifest) : entry;
    });
}

export async function resolveProfileSummary(summary: ProfileSummary): Promise<ProfileSummary> {
    const [systems, zones] = await Promise.all([
        resolveEntries(summary.systems),
        resolveEntries(summary.zones),
    ]);
    return { ...summary, systems, zones };
}

export async function resolveProfileFrameDetails(
    response: ProfileFrameDetails,
): Promise<ProfileFrameDetails> {
    const systemEntries = response.details.flatMap((detail) => detail.systems);
    const zoneEntries = response.details.flatMap((detail) => detail.zones);
    const [resolvedSystems, resolvedZones] = await Promise.all([
        resolveEntries(systemEntries),
        resolveEntries(zoneEntries),
    ]);
    let systemOffset = 0;
    let zoneOffset = 0;
    return {
        ...response,
        details: response.details.map((detail) => {
            const systems = resolvedSystems.slice(
                systemOffset,
                systemOffset + detail.systems.length,
            );
            const zones = resolvedZones.slice(zoneOffset, zoneOffset + detail.zones.length);
            systemOffset += detail.systems.length;
            zoneOffset += detail.zones.length;
            return { ...detail, systems, zones };
        }),
    };
}
