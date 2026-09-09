import type {
    ProfileEntry,
    ProfileFrameDetails,
    ProfileSummary,
} from "../runtime/profiling";

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
    manifest: Promise<ProfileSymbolManifest | null>;
}

const manifests = new Map<string, ProfileSymbolCache>();

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

async function loadManifest(moduleId: string): Promise<ProfileSymbolManifest | null> {
    let cache = manifests.get(moduleId);
    if (!cache) {
        const match = /^wasm:([0-9a-f]{64})$/.exec(moduleId);
        const manifest = match
            ? fetch(new URL(`profile-symbols/${match[1]}.json`, document.baseURI), {
                  cache: "no-store",
              })
                  .then(async (response) => {
                      if (!response.ok) {
                          throw new Error(`Profile symbols unavailable (${response.status}).`);
                      }
                      const value: unknown = await response.json();
                      if (!isManifest(value, moduleId)) {
                          throw new Error("Invalid profiling symbol manifest.");
                      }
                      return value;
                  })
                  .catch(() => null)
            : Promise.resolve(null);
        cache = { manifest };
        manifests.set(moduleId, cache);
    }
    return cache.manifest;
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
    const moduleIds = new Set<string>();
    for (const entry of entries) {
        if (entry.symbol?.kind !== "wasm-function-index") continue;
        moduleIds.add(entry.symbol.moduleId);
    }
    const loaded = await Promise.all(
        [...moduleIds].map(
            async (moduleId) => [moduleId, await loadManifest(moduleId)] as const,
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
