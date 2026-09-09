import {
    ExtensionHostKind,
    registerExtension,
    type RegisterLocalExtensionResult,
} from "@codingame/monaco-vscode-api/extensions";
import materialIconThemeUrl from "@material-icon-theme/dist/material-icons.json?url";

const materialIconUrls = import.meta.glob(
    "@material-icon-theme/icons/*.svg",
    {
        eager: true,
        import: "default",
        query: "?url",
    },
) as Record<string, string>;

let materialIconExtension: RegisterLocalExtensionResult | undefined;

export function registerMaterialIconTheme(): void {
    if (materialIconExtension) return;

    materialIconExtension = registerExtension(
        {
            name: "material-icon-theme",
            displayName: "Material Icon Theme",
            publisher: "PKief",
            version: "5.37.0",
            engines: { vscode: "^1.55.0" },
            contributes: {
                iconThemes: [
                    {
                        id: "material-icon-theme",
                        label: "Material Icon Theme",
                        path: "./dist/material-icons.json",
                    },
                ],
            },
        },
        ExtensionHostKind.LocalWebWorker,
        { system: true },
    );

    materialIconExtension.registerFileUrl(
        "./dist/material-icons.json",
        materialIconThemeUrl,
    );
    for (const [sourcePath, iconUrl] of Object.entries(materialIconUrls)) {
        const fileName = sourcePath.slice(sourcePath.lastIndexOf("/") + 1);
        materialIconExtension.registerFileUrl(`./icons/${fileName}`, iconUrl);
    }
}
