import type { ProjectFileEntry } from "./types";

export interface FileTreeNode {
    name: string;
    path: string;
    entry?: ProjectFileEntry;
    children: FileTreeNode[];
}

function assetRelativePath(path: string): string {
    const prefix = "assets/";
    return path.startsWith(prefix) ? path.slice(prefix.length) : path;
}

function isDirectory(node: FileTreeNode): boolean {
    return !node.entry || node.entry.kind === "directory";
}

export function buildFileTree(files: ProjectFileEntry[]): FileTreeNode[] {
    const root: FileTreeNode = { name: "", path: "", children: [] };
    for (const entry of files) {
        let parent = root;
        const displayPath = assetRelativePath(entry.path);
        const parts = displayPath.split("/");
        parts.forEach((name, index) => {
            const path = parts.slice(0, index + 1).join("/");
            let node = parent.children.find((candidate) => candidate.name === name);
            if (!node) {
                node = { name, path, children: [] };
                parent.children.push(node);
            }
            if (index === parts.length - 1) node.entry = entry;
            parent = node;
        });
    }
    const sort = (nodes: FileTreeNode[]) => {
        nodes.sort((left, right) => {
            const leftIsDirectory = isDirectory(left);
            const rightIsDirectory = isDirectory(right);
            if (leftIsDirectory !== rightIsDirectory) return leftIsDirectory ? -1 : 1;
            return left.name.localeCompare(right.name);
        });
        nodes.forEach((node) => sort(node.children));
    };
    sort(root.children);
    return root.children;
}
