import type * as Monaco from "monaco-editor";
import {
    ExtensionHostKind,
    registerExtension,
    type RegisterLocalExtensionResult,
} from "@codingame/monaco-vscode-api/extensions";
import luauGrammarUrl from "./luau.tmLanguage.json?url";

const languageId = "luau";
export const luauEditorTheme = "entisium-dark";
const textmateScope = "source.luau";
let luauExtension: RegisterLocalExtensionResult | undefined;

export function registerLuauExtension(): void {
    if (luauExtension) return;

    luauExtension = registerExtension(
        {
            name: "entisium-luau",
            displayName: "Entisium Luau",
            publisher: "entisium",
            version: "1.0.0",
            engines: { vscode: "*" },
            contributes: {
                languages: [
                    {
                        id: languageId,
                        aliases: ["Luau", "luau"],
                        extensions: [".luau"],
                        mimetypes: ["text/x-luau"],
                    },
                ],
                grammars: [
                    {
                        language: languageId,
                        scopeName: textmateScope,
                        path: "./syntaxes/luau.tmLanguage.json",
                    },
                ],
            },
        },
        ExtensionHostKind.LocalWebWorker,
        { system: true },
    );
    luauExtension.registerFileUrl(
        "./syntaxes/luau.tmLanguage.json",
        luauGrammarUrl,
    );
}

const configuration: Monaco.languages.LanguageConfiguration = {
    comments: {
        lineComment: "--",
        blockComment: ["--[[", "]]"],
    },
    brackets: [
        ["{", "}"],
        ["[", "]"],
        ["(", ")"],
    ],
    autoClosingPairs: [
        { open: "{", close: "}" },
        { open: "[", close: "]" },
        { open: "(", close: ")" },
        { open: '"', close: '"', notIn: ["string", "comment"] },
        { open: "'", close: "'", notIn: ["string", "comment"] },
        { open: "`", close: "`", notIn: ["string", "comment"] },
    ],
    surroundingPairs: [
        { open: "{", close: "}" },
        { open: "[", close: "]" },
        { open: "(", close: ")" },
        { open: '"', close: '"' },
        { open: "'", close: "'" },
        { open: "`", close: "`" },
    ],
    indentationRules: {
        increaseIndentPattern:
            /^\s*(?:(?:if|elseif)\b.*\bthen|(?:for|while)\b.*\bdo|function\b.*|do|else|repeat)\s*$/,
        decreaseIndentPattern: /^\s*(?:end|else|elseif|until)\b/,
    },
};

const language: Monaco.languages.IMonarchLanguage = {
    defaultToken: "",
    tokenPostfix: ".luau",

    keywords: [
        "and",
        "break",
        "continue",
        "do",
        "else",
        "elseif",
        "end",
        "export",
        "false",
        "for",
        "function",
        "if",
        "in",
        "local",
        "nil",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "true",
        "type",
        "until",
        "while",
    ],

    typeKeywords: [
        "any",
        "boolean",
        "buffer",
        "never",
        "number",
        "string",
        "table",
        "thread",
        "unknown",
        "userdata",
        "vector",
    ],

    builtins: [
        "assert",
        "collectgarbage",
        "error",
        "gcinfo",
        "getfenv",
        "getmetatable",
        "ipairs",
        "loadstring",
        "newproxy",
        "next",
        "pairs",
        "pcall",
        "print",
        "rawequal",
        "rawget",
        "rawlen",
        "rawset",
        "require",
        "select",
        "setfenv",
        "setmetatable",
        "tonumber",
        "tostring",
        "typeof",
        "unpack",
        "xpcall",
    ],

    operators: [
        "+",
        "-",
        "*",
        "/",
        "//",
        "%",
        "^",
        "#",
        "==",
        "~=",
        "<=",
        ">=",
        "<",
        ">",
        "=",
        "+=",
        "-=",
        "*=",
        "/=",
        "//=",
        "%=",
        "^=",
        "..=",
        "->",
        "|",
        "&",
        "?",
    ],

    symbols: /[=><!~?:&|+\-*\/\^%#]+/,
    escapes: /\\(?:[abfnrtv\\"'`]|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]+\}|u[0-9A-Fa-f]{4}|z|\d{1,3})/,

    tokenizer: {
        root: [
            [
                /(local)(\s+)(function)(\s+)([a-zA-Z_]\w*)/,
                ["keyword", "", "keyword", "", "function.declaration"],
            ],
            [
                /(function)(\s+)([a-zA-Z_]\w*)/,
                ["keyword", "", "function.declaration"],
            ],
            [/(local)(\s+)([a-zA-Z_]\w*)/, ["keyword", "", "variable"]],
            [
                /(for)(\s+)([a-zA-Z_]\w*)/,
                ["keyword", "", "variable"],
            ],
            [
                /([a-zA-Z_]\w*)(\s*)(:)(?=\s*(?:[A-Z]|any\b|boolean\b|buffer\b|never\b|nil\b|number\b|string\b|table\b|thread\b|unknown\b|userdata\b|vector\b|[({]))/,
                ["variable.parameter", "", "delimiter"],
            ],
            [
                /([.:])(\s*)([a-zA-Z_]\w*)(?=\s*\()/,
                ["delimiter", "", "function.call"],
            ],
            [
                /([.:])(\s*)([a-zA-Z_]\w*)/,
                ["delimiter", "", "variable.property"],
            ],
            [/[A-Z][a-zA-Z0-9_]*/, "type.identifier"],
            [
                /[a-zA-Z_]\w*(?=\s*\()/,
                {
                    cases: {
                        "@keywords": "keyword",
                        "@builtins": "function.call",
                        "@default": "function.call",
                    },
                },
            ],
            [
                /[a-zA-Z_]\w*/,
                {
                    cases: {
                        "@keywords": "keyword",
                        "@typeKeywords": "type",
                        "@builtins": "predefined",
                        "@default": "variable",
                    },
                },
            ],
            [/@[a-zA-Z_]\w*/, "annotation"],
            { include: "@whitespace" },
            [/\[([=]*)\[/, "string.quote", "@longString.$1"],
            [/[{}()\[\]]/, "@brackets"],
            [
                /@symbols/,
                {
                    cases: {
                        "@operators": "operator",
                        "@default": "delimiter",
                    },
                },
            ],
            [/\.\.\.=|\.\.=|\.\.\.|\.\./, "operator"],
            [/0[bB][01](?:_?[01])*/, "number.binary"],
            [/0[xX][0-9a-fA-F](?:_?[0-9a-fA-F])*(?:\.[0-9a-fA-F](?:_?[0-9a-fA-F])*)?(?:[pP][+-]?\d(?:_?\d)*)?/, "number.hex"],
            [/\d(?:_?\d)*\.\d(?:_?\d)*(?:[eE][+-]?\d(?:_?\d)*)?/, "number.float"],
            [/\d(?:_?\d)*(?:[eE][+-]?\d(?:_?\d)*)/, "number.float"],
            [/\d(?:_?\d)*/, "number"],
            [/[;,.]/, "delimiter"],
            [/"/, "string.quote", "@quotedString.\""],
            [/'/, "string.quote", "@quotedString.'"],
            [/`/, "string.quote", "@interpolatedString"],
        ],

        whitespace: [
            [/[ \t\r\n]+/, ""],
            [/---.*$/, "comment.doc"],
            [/--\[([=]*)\[/, "comment", "@blockComment.$1"],
            [/--.*$/, "comment"],
        ],

        blockComment: [
            [/[^\]]+/, "comment"],
            [
                /\]([=]*)\]/,
                {
                    cases: {
                        "$1==$S2": { token: "comment", next: "@pop" },
                        "@default": "comment",
                    },
                },
            ],
            [/./, "comment"],
        ],

        longString: [
            [/[^\]]+/, "string"],
            [
                /\]([=]*)\]/,
                {
                    cases: {
                        "$1==$S2": { token: "string.quote", next: "@pop" },
                        "@default": "string",
                    },
                },
            ],
            [/./, "string"],
        ],

        quotedString: [
            [/[^\\"']+/, "string"],
            [/@escapes/, "string.escape"],
            [/\\./, "string.escape.invalid"],
            [
                /["']/,
                {
                    cases: {
                        "$#==$S2": { token: "string.quote", next: "@pop" },
                        "@default": "string",
                    },
                },
            ],
        ],

        interpolatedString: [
            [/`/, "string.quote", "@pop"],
            [/\{/, "delimiter.bracket", "@interpolation"],
            [/@escapes/, "string.escape"],
            [/\\./, "string.escape.invalid"],
            [/[^\\{`]+/, "string"],
            [/./, "string"],
        ],

        interpolation: [
            [/\}/, "delimiter.bracket", "@pop"],
            [/\{/, "delimiter.bracket", "@interpolation"],
            { include: "@root" },
        ],
    },
};

export function registerLuauLanguage(
    monaco: typeof Monaco,
    options: { registerTheme?: boolean } = {},
): void {
    if (!monaco.languages.getLanguages().some(({ id }) => id === languageId)) {
        monaco.languages.register({
            id: languageId,
            aliases: ["Luau", "luau"],
            extensions: [".luau"],
            mimetypes: ["text/x-luau"],
        });
    }

    monaco.languages.setLanguageConfiguration(languageId, configuration);
    if (options.registerTheme === false) return;
    monaco.languages.setMonarchTokensProvider(languageId, language);

    monaco.editor.defineTheme(luauEditorTheme, {
        base: "vs-dark",
        inherit: false,
        rules: [
            { token: "luau.variable", foreground: "BCBEC8" },
            { token: "luau.support.variable", foreground: "BCBEC8" },
            { token: "luau.keyword.operator", foreground: "BCBEC8" },
            { token: "luau.source", foreground: "BCBEC8" },
            { token: "luau.constant.numeric", foreground: "F2BA2A" },
            { token: "luau.string", foreground: "8EE9B6" },
            { token: "luau.comment", foreground: "6A6F81" },
            { token: "luau.keyword.operator.wordlike", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.storage.modifier.visibility", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.keyword.control", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.keyword.operator.attribute", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.constant.language", foreground: "8FB4FF" },
            { token: "luau.support.constant", foreground: "8FB4FF" },
            { token: "luau.variable.language", foreground: "8FB4FF" },
            { token: "luau.support.function", foreground: "8FB4FF" },
            { token: "luau.variable.other.property", foreground: "70A0FF" },
            { token: "luau.variable.language.metamethod", foreground: "70A0FF" },
            { token: "luau.constant.language.nil", foreground: "F2BA2A", fontStyle: "bold" },
            { token: "luau.constant.language.boolean", foreground: "F2BA2A", fontStyle: "bold" },
            { token: "luau.meta.function", foreground: "EB7973" },
            { token: "luau.storage.modifier.local", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.variable.language.self", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.storage.modifier", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.storage.type", foreground: "EB7973", fontStyle: "bold" },
            { token: "luau.entity.name.function", foreground: "FAE4AA" },
            { token: "luau.entity.name.type", foreground: "70A0FF" },
            { token: "luau.punctuation", foreground: "BCBEC8" },
            { token: "luau.support.type", foreground: "70A0FF" },

            // Monarch is retained as a lightweight fallback while Oniguruma loads.
            { token: "comment.luau", foreground: "6A6F81" },
            { token: "string.luau", foreground: "8EE9B6" },
            { token: "number.luau", foreground: "F2BA2A" },
            { token: "keyword.luau", foreground: "EB7973", fontStyle: "bold" },
            { token: "operator.luau", foreground: "BCBEC8" },
            { token: "delimiter.luau", foreground: "BCBEC8" },
            { token: "type.luau", foreground: "70A0FF" },
            { token: "type.identifier.luau", foreground: "70A0FF" },
            { token: "function.declaration.luau", foreground: "FAE4AA" },
            { token: "function.call.luau", foreground: "FAE4AA" },
            { token: "variable.luau", foreground: "BCBEC8" },
            { token: "variable.property.luau", foreground: "70A0FF" },
            { token: "predefined.luau", foreground: "8FB4FF" },
        ],
        colors: {
            "editor.background": "#191919",
            "editor.foreground": "#BCBEC8",
            "editor.lineHighlightBackground": "#2B2B2B",
            "editor.selectionBackground": "#264F78",
            "editor.inactiveSelectionBackground": "#333333",
            "editorLineNumber.foreground": "#666666",
            "editorLineNumber.activeForeground": "#BCBEC8",
            "editorWhitespace.foreground": "#404040",
            "editorIndentGuide.background1": "#2B2B2B",
            "editorIndentGuide.activeBackground1": "#4A4A4A",
            "editorCursor.foreground": "#BCBEC8",
        },
    });
}
