export function documentUri(rootUri: string, path: string): string {
    const encodedPath = path.split("/").map(encodeURIComponent).join("/");
    if (!rootUri) return `file:///project/${encodedPath}`;
    const base = rootUri.endsWith("/") ? rootUri : `${rootUri}/`;
    return `${base}${encodedPath}`;
}

interface MemoryDirectoryPlan {
    driveRoot?: string;
    directories: string[];
}

export function planMemoryDirectories(path: string): MemoryDirectoryPlan {
    const pathSegments = path.split("/").filter(Boolean);
    const driveRoot = /^[a-zA-Z]:$/.test(pathSegments[0] ?? "")
        ? `/${pathSegments[0]}`
        : undefined;
    const firstDirectory = driveRoot ? 2 : 1;
    const directories: string[] = [];
    for (let index = firstDirectory; index < pathSegments.length; index += 1) {
        directories.push(`/${pathSegments.slice(0, index).join("/")}`);
    }
    return { driveRoot, directories };
}
