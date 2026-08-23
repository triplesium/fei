import type { KeyboardEvent } from "react";
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

export type ProjectOperation = "new" | "new-folder" | "rename" | "delete" | null;

interface ProjectOperationDialogProps {
    operation: ProjectOperation;
    path: string;
    error?: string;
    onOpenChange(open: boolean): void;
    onPathChange(path: string): void;
    onApply(): void | Promise<void>;
}

const titles: Record<Exclude<ProjectOperation, null>, string> = {
    new: "Create file",
    "new-folder": "Create folder",
    rename: "Rename file",
    delete: "Delete file",
};

export function ProjectOperationDialog({
    operation,
    path,
    error,
    onOpenChange,
    onPathChange,
    onApply,
}: ProjectOperationDialogProps) {
    const destructive = operation === "delete";
    const submitOnEnter = (event: KeyboardEvent<HTMLInputElement>): void => {
        if (event.key === "Enter") void onApply();
    };

    return (
        <Dialog open={operation !== null} onOpenChange={onOpenChange}>
            <DialogContent className="w-[min(430px,calc(100vw-30px))]">
                <DialogHeader>
                    <DialogTitle>{operation ? titles[operation] : "File operation"}</DialogTitle>
                    <DialogDescription>
                        {destructive
                            ? "This removes the file from the project's asset directory."
                            : operation === "new-folder"
                              ? "The folder path is relative to the project’s Assets root."
                              : "Paths are relative to the project’s Assets root."}
                    </DialogDescription>
                </DialogHeader>
                <label className="grid gap-1.5 text-[10px] font-semibold text-[#b7bdc8]" htmlFor="operation-path">
                    <span>Asset path</span>
                    <Input
                        id="operation-path"
                        value={path}
                        readOnly={destructive}
                        autoFocus
                        spellCheck={false}
                        className="font-mono read-only:bg-white/[0.03] read-only:text-muted-foreground"
                        onChange={(event) => onPathChange(event.target.value)}
                        onKeyDown={submitOnEnter}
                    />
                </label>
                {error && (
                    <p className="m-0 rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-[10px] leading-4 text-[#ff9aaa]">
                        {error}
                    </p>
                )}
                <DialogFooter>
                    <DialogClose asChild>
                        <Button variant="outline">Cancel</Button>
                    </DialogClose>
                    <Button variant={destructive ? "destructive" : "default"} onClick={() => void onApply()}>
                        {destructive ? "Delete" : "Apply"}
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );
}
