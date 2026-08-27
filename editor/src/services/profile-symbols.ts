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

const manifests = new Map<string, Promise<ProfileSymbolManifest | null>>();

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
    let pending = manifests.get(moduleId);
    if (!pending) {
        pending = editorHost
            .json<unknown>(`/api/v1/profile-symbols?module=${encodeURIComponent(moduleId)}`)
            .then((value) => (isManifest(value, moduleId) ? value : null))
            .catch(() => null);
        manifests.set(moduleId, pending);
    }
    return pending;
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
    const moduleIds = [
        ...new Set(
            entries
                .filter((entry) => entry.symbol?.kind === "wasm-function-index")
                .map((entry) => entry.symbol!.moduleId),
        ),
    ];
    const loaded = await Promise.all(
        moduleIds.map(async (moduleId) => [moduleId, await loadManifest(moduleId)] as const),
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
    return {
        ...response,
        details: await Promise.all(
            response.details.map(async (detail) => {
                const [systems, zones] = await Promise.all([
                    resolveEntries(detail.systems),
                    resolveEntries(detail.zones),
                ]);
                return { ...detail, systems, zones };
            }),
        ),
    };
}
