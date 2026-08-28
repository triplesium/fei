import {
    Box,
    File,
    FileCode2,
    Folder,
    Image,
    Pencil,
    Trash2,
} from "lucide-react";
import { useEffect, useState } from "react";
import type { ProjectAssetInspection } from "@/types";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { PanelEmptyState, PanelSection, PanelSectionTitle } from "@/components/panel";

interface ResourceInspectorProps {
    inspection: ProjectAssetInspection | null;
    loading: boolean;
    error?: string;
    previewUrl?: string;
    onOpen(): void;
    onRename(): void;
    onDelete(): void;
}

function formatBytes(bytes: number | undefined): string {
    if (bytes === undefined) return "—";
    if (bytes < 1024) return `${bytes} B`;
    const units = ["KB", "MB", "GB"];
    let value = bytes / 1024;
    let unit = units[0];
    for (let index = 1; index < units.length && value >= 1024; index += 1) {
        value /= 1024;
        unit = units[index];
    }
    return `${value >= 10 ? value.toFixed(1) : value.toFixed(2)} ${unit}`;
}

function formatDate(value: string): string {
    const date = new Date(value);
    return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function assetTypeLabel(inspection: ProjectAssetInspection): string {
    if (inspection.assetType === "folder") return "Folder";
    if (inspection.assetType === "script") return inspection.extension === ".luau" ? "Luau Script" : "Lua Script";
    if (inspection.assetType === "image") return "Image";
    if (inspection.assetType === "model") return "3D Model";
    if (inspection.assetType === "text") return "Text";
    return "Binary";
}

function AssetIcon({ inspection }: { inspection: ProjectAssetInspection }) {
    const props = { className: "size-5", strokeWidth: 1.6 };
    if (inspection.assetType === "folder") return <Folder {...props} />;
    if (inspection.assetType === "script") return <FileCode2 {...props} />;
    if (inspection.assetType === "image") return <Image {...props} />;
    if (inspection.assetType === "model") return <Box {...props} />;
    return <File {...props} />;
}

function Property({ label, value }: { label: string; value: string | number | undefined }) {
    if (value === undefined) return null;
    return (
        <div className="grid grid-cols-[74px_minmax(0,1fr)] border-b border-border/55 py-1.5 text-[12px] last:border-b-0">
            <dt className="text-muted-foreground">{label}</dt>
            <dd className="m-0 min-w-0 truncate text-right text-[#b7bdc8]" title={String(value)}>
                {value}
            </dd>
        </div>
    );
}

export function ResourceInspector({
    inspection,
    loading,
    error,
    previewUrl,
    onOpen,
    onRename,
    onDelete,
}: ResourceInspectorProps) {
    const [imageSize, setImageSize] = useState<{ width: number; height: number } | null>(null);
    useEffect(() => setImageSize(null), [previewUrl]);

    if (loading && !inspection) return <PanelEmptyState>Inspecting asset…</PanelEmptyState>;
    if (error && !inspection) return <PanelEmptyState>{error}</PanelEmptyState>;
    if (!inspection) return <PanelEmptyState>Select an asset to inspect.</PanelEmptyState>;

    const name = inspection.path.split("/").at(-1) ?? inspection.path;
    const settings = Object.entries(inspection.metadata?.settings ?? {});
    return (
        <div className="min-h-full" aria-busy={loading}>
            <PanelSection className="pb-3">
                <div className="flex min-w-0 items-start gap-2.5">
                    <div className="grid size-9 shrink-0 place-items-center rounded-md border border-border/60 bg-muted/40 text-primary">
                        <AssetIcon inspection={inspection} />
                    </div>
                    <div className="min-w-0 flex-1">
                        <div className="truncate text-[13px] font-medium text-foreground" title={name}>{name}</div>
                        <div className="mt-1 flex flex-wrap items-center gap-1">
                            <Badge>{assetTypeLabel(inspection)}</Badge>
                            {inspection.metadata && <Badge>{inspection.metadata.state}</Badge>}
                        </div>
                    </div>
                </div>
                <div className="mt-3 flex flex-wrap gap-1.5">
                    {inspection.kind === "text" && !inspection.readonly && (
                        <Button
                            variant="secondary"
                            size="sm"
                            type="button"
                            disabled={loading}
                            onClick={onOpen}
                        >
                            Open
                        </Button>
                    )}
                    {inspection.kind !== "directory" && (
                        <Button
                            variant="ghost"
                            size="sm"
                            type="button"
                            disabled={loading}
                            onClick={onRename}
                        >
                            <Pencil className="size-3.5" strokeWidth={1.7} /> Rename
                        </Button>
                    )}
                    <Button
                        variant="ghost"
                        size="sm"
                        type="button"
                        className="text-destructive"
                        disabled={loading}
                        onClick={onDelete}
                    >
                        <Trash2 className="size-3.5" strokeWidth={1.7} /> Delete
                    </Button>
                </div>
            </PanelSection>

            {inspection.assetType === "image" && (
                <>
                    <Separator />
                    <PanelSection>
                        <PanelSectionTitle>PREVIEW</PanelSectionTitle>
                        <div className="mt-2 grid min-h-32 place-items-center overflow-hidden rounded-md border border-border/60 bg-[repeating-conic-gradient(#252525_0_25%,#303030_0_50%)_0_0/16px_16px] p-2">
                            {previewUrl ? (
                                <img
                                    src={previewUrl}
                                    alt={name}
                                    className="max-h-56 max-w-full object-contain"
                                    onLoad={(event) => setImageSize({
                                        width: event.currentTarget.naturalWidth,
                                        height: event.currentTarget.naturalHeight,
                                    })}
                                />
                            ) : (
                                <span className="text-[11px] text-muted-foreground">Preview unavailable</span>
                            )}
                        </div>
                    </PanelSection>
                </>
            )}

            <Separator />
            <PanelSection>
                <PanelSectionTitle>ASSET</PanelSectionTitle>
                <dl className="mt-2">
                    <Property label="Path" value={inspection.path.replace(/^assets\//, "")} />
                    <Property label="Type" value={assetTypeLabel(inspection)} />
                    <Property label="Extension" value={inspection.extension || "—"} />
                    <Property label="Size" value={formatBytes(inspection.size)} />
                    <Property label="Modified" value={formatDate(inspection.modifiedAt)} />
                    <Property label="Lines" value={inspection.lineCount} />
                    <Property label="Dimensions" value={imageSize ? `${imageSize.width} × ${imageSize.height}` : undefined} />
                    <Property label="Files" value={inspection.fileCount} />
                    <Property label="Folders" value={inspection.directoryCount} />
                    <Property label="Vertices" value={inspection.vertexCount} />
                    <Property label="Faces" value={inspection.faceCount} />
                    <Property label="Nodes" value={inspection.nodeCount} />
                    <Property label="Meshes" value={inspection.meshCount} />
                    <Property label="Materials" value={inspection.materialCount} />
                </dl>
            </PanelSection>

            {(inspection.metadata || inspection.metadataError) && (
                <>
                    <Separator />
                    <PanelSection>
                        <PanelSectionTitle>IMPORT</PanelSectionTitle>
                        {inspection.metadata ? (
                            <>
                                <dl className="mt-2">
                                    <Property label="Importer" value={inspection.metadata.importer} />
                                    <Property label="UUID" value={inspection.metadata.id} />
                                    <Property label="State" value={inspection.metadata.state} />
                                </dl>
                                {settings.length > 0 && (
                                    <dl className="mt-2 rounded-md border border-border/55 px-2">
                                        {settings.map(([name, value]) => <Property key={name} label={name} value={value} />)}
                                    </dl>
                                )}
                            </>
                        ) : (
                            <p className="m-0 mt-2 text-[11px] leading-relaxed text-destructive">{inspection.metadataError}</p>
                        )}
                    </PanelSection>
                </>
            )}
        </div>
    );
}
