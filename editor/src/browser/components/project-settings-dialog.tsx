import type { ReactNode } from "react";
import type { ProjectSettings } from "@/types";
import { Button } from "@/components/ui/button";
import {
    Dialog,
    DialogClose,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Textarea } from "@/components/ui/textarea";

interface ProjectSettingsDialogProps {
    open: boolean;
    draft: ProjectSettings;
    plugins: string;
    error?: string;
    onOpenChange(open: boolean): void;
    onNameChange(name: string): void;
    onPluginsChange(plugins: string): void;
    onSave(): void | Promise<void>;
}

function Field({ label, hint, children }: { label: string; hint?: string; children: ReactNode }) {
    return (
        <label className="grid gap-1.5 text-[10px] font-semibold text-[#b7bdc8]">
            <span>
                {label}
                {hint && (
                    <small className="ml-1.5 text-[10px] font-normal text-muted-foreground">{hint}</small>
                )}
            </span>
            {children}
        </label>
    );
}

export function ProjectSettingsDialog({
    open,
    draft,
    plugins,
    error,
    onOpenChange,
    onNameChange,
    onPluginsChange,
    onSave,
}: ProjectSettingsDialogProps) {
    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>Project settings</DialogTitle>
                    <DialogDescription>
                        Configure project-level metadata managed in project.yaml.
                    </DialogDescription>
                </DialogHeader>
                <div className="grid gap-4">
                    <Field label="Project name">
                        <Input value={draft.name} autoFocus onChange={(event) => onNameChange(event.target.value)} />
                    </Field>
                    <details className="group rounded-lg border border-border/80 bg-white/[0.02]">
                        <summary className="cursor-pointer list-none px-3 py-2.5 text-[10px] font-semibold text-[#b7bdc8] outline-none [&::-webkit-details-marker]:hidden">
                            Advanced
                        </summary>
                        <div className="grid gap-4 border-t border-border/70 p-3">
                            <Field label="Asset directory" hint="Managed by the development editor">
                                <Input
                                    value={draft.assetDirectory}
                                    readOnly
                                    className="font-mono read-only:bg-white/[0.03] read-only:text-muted-foreground"
                                />
                            </Field>
                            <Field label="Runtime plugins" hint="One qualified plugin id per line">
                                <Textarea
                                    rows={5}
                                    spellCheck={false}
                                    value={plugins}
                                    placeholder="project_runtime::LuauScripts"
                                    onChange={(event) => onPluginsChange(event.target.value)}
                                />
                            </Field>
                        </div>
                    </details>
                    {error && (
                        <p className="m-0 rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-[10px] leading-4 text-[#ff9aaa]">
                            {error}
                        </p>
                    )}
                </div>
                <DialogFooter>
                    <DialogClose asChild>
                        <Button variant="outline">Cancel</Button>
                    </DialogClose>
                    <Button onClick={() => void onSave()}>Save settings</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );
}
