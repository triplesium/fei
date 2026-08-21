import {spawn} from "node:child_process";
import {existsSync} from "node:fs";
import {mkdtemp, readFile, rm, stat} from "node:fs/promises";
import {createServer, get} from "node:http";
import {tmpdir} from "node:os";
import {basename, extname, isAbsolute, join, relative, resolve} from "node:path";

const argumentsByName = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
    argumentsByName.set(process.argv[index], process.argv[index + 1]);
}

const outputRoot = resolve(argumentsByName.get("--root") ?? "");
const timeoutMs = Number(argumentsByName.get("--timeout") ?? "30000");
const requestedBrowser = argumentsByName.get("--browser");

if (!outputRoot || !Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    throw new Error("usage: browser_smoke.mjs --root <directory> [--browser <path>] [--timeout <milliseconds>]");
}

const mimeTypes = new Map([
    [".data", "application/octet-stream"],
    [".html", "text/html; charset=utf-8"],
    [".js", "text/javascript; charset=utf-8"],
    [".wasm", "application/wasm"],
]);

function browserCandidates() {
    if (requestedBrowser) {
        return [requestedBrowser];
    }
    if (process.env.FEI_BROWSER) {
        return [process.env.FEI_BROWSER];
    }
    if (process.platform === "win32") {
        return [
            join(process.env["ProgramFiles(x86)"] ?? "", "Microsoft/Edge/Application/msedge.exe"),
            join(process.env.ProgramFiles ?? "", "Microsoft/Edge/Application/msedge.exe"),
            join(process.env.LOCALAPPDATA ?? "", "Microsoft/Edge/Application/msedge.exe"),
            join(process.env.ProgramFiles ?? "", "Google/Chrome/Application/chrome.exe"),
            join(process.env["ProgramFiles(x86)"] ?? "", "Google/Chrome/Application/chrome.exe"),
        ];
    }
    if (process.platform === "darwin") {
        return [
            "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
        ];
    }
    return [
        "/usr/bin/microsoft-edge",
        "/usr/bin/google-chrome",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
    ];
}

function findBrowser() {
    const browser = browserCandidates().find(candidate => candidate && existsSync(candidate));
    if (!browser) {
        throw new Error("Edge, Chrome, or Chromium was not found; pass --browser or set FEI_BROWSER");
    }
    return browser;
}

async function stopBrowserProcesses(profileDirectory, browserProcess) {
    if (process.platform === "win32") {
        const script = [
            "$profilePath = $env:FEI_SMOKE_PROFILE",
            "$processes = @(Get-CimInstance Win32_Process -Filter \"Name = 'msedge.exe' OR Name = 'chrome.exe'\" | Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($profilePath, [StringComparison]::OrdinalIgnoreCase) -ge 0 })",
            "$processes | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }",
        ].join("; ");
        const cleanup = spawn("powershell.exe", [
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            script,
        ], {
            env: {...process.env, FEI_SMOKE_PROFILE: profileDirectory},
            stdio: "ignore",
        });
        await new Promise(accept => cleanup.once("exit", accept));
        return;
    }
    if (browserProcess.exitCode === null) {
        browserProcess.kill("SIGTERM");
    }
}

async function startServer() {
    const server = createServer(async (request, response) => {
        try {
            const url = new URL(request.url ?? "/", "http://127.0.0.1");
            if (url.pathname === "/favicon.ico") {
                response.writeHead(204).end();
                return;
            }
            const requestedPath = decodeURIComponent(url.pathname === "/" ? "/sample-browser.html" : url.pathname);
            const filePath = resolve(outputRoot, `.${requestedPath}`);
            const relativePath = relative(outputRoot, filePath);
            if (relativePath.startsWith("..") || isAbsolute(relativePath)) {
                response.writeHead(403).end("Forbidden");
                return;
            }
            if (!(await stat(filePath)).isFile()) {
                response.writeHead(404).end("Not found");
                return;
            }
            response.writeHead(200, {
                "Cache-Control": "no-store",
                "Content-Type": mimeTypes.get(extname(filePath)) ?? "application/octet-stream",
            });
            response.end(await readFile(filePath));
        } catch {
            response.writeHead(404).end("Not found");
        }
    });
    await new Promise((accept, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", accept);
    });
    return server;
}

async function unusedPort() {
    const server = createServer();
    await new Promise((accept, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", accept);
    });
    const port = server.address().port;
    const serverClosed = new Promise(accept => server.close(accept));
    server.closeAllConnections();
    await serverClosed;
    return port;
}

function getJson(url) {
    return new Promise((accept, reject) => {
        const request = get(url, response => {
            let body = "";
            response.setEncoding("utf8");
            response.on("data", chunk => body += chunk);
            response.on("end", () => {
                try {
                    accept(JSON.parse(body));
                } catch (error) {
                    reject(error);
                }
            });
        });
        request.setTimeout(500, () => request.destroy(new Error("request timed out")));
        request.on("error", reject);
    });
}

async function waitForDevTools(browserProcess, port) {
    let stderr = "";
    let exitCode;
    browserProcess.stderr.setEncoding("utf8");
    browserProcess.stderr.on("data", chunk => stderr += chunk);
    browserProcess.once("exit", code => exitCode = code);
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (exitCode !== undefined && exitCode !== 0) {
            throw new Error(`browser exited before DevTools was ready (${exitCode})\n${stderr}`);
        }
        try {
            const version = await getJson(`http://127.0.0.1:${port}/json/version`);
            if (version.webSocketDebuggerUrl) {
                return version.webSocketDebuggerUrl;
            }
        } catch {
            // The debugging endpoint is not listening yet.
        }
        await new Promise(accept => setTimeout(accept, 50));
    }
    throw new Error(`timed out waiting for browser DevTools\n${stderr}`);
}

async function waitForPageTarget(devToolsUrl) {
    const endpoint = new URL(devToolsUrl);
    const listUrl = `http://${endpoint.hostname}:${endpoint.port}/json/list`;
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        const targets = await getJson(listUrl);
        const page = targets.find(target => target.type === "page");
        if (page?.webSocketDebuggerUrl) {
            return page.webSocketDebuggerUrl;
        }
        await new Promise(accept => setTimeout(accept, 50));
    }
    throw new Error("timed out waiting for a browser page target");
}

class DevToolsClient {
    constructor(url) {
        this.nextId = 1;
        this.pending = new Map();
        this.listeners = new Map();
        this.socket = new WebSocket(url);
        this.opened = new Promise((accept, reject) => {
            const timer = setTimeout(() => reject(new Error("timed out connecting to browser DevTools")), timeoutMs);
            this.socket.addEventListener("open", () => {
                clearTimeout(timer);
                accept();
            }, {once: true});
            this.socket.addEventListener("error", () => {
                clearTimeout(timer);
                reject(new Error("failed to connect to browser DevTools"));
            }, {once: true});
        });
        this.socket.addEventListener("message", event => {
            const message = JSON.parse(event.data);
            if (message.id) {
                const pending = this.pending.get(message.id);
                if (!pending) {
                    return;
                }
                this.pending.delete(message.id);
                if (message.error) {
                    pending.reject(new Error(message.error.message));
                } else {
                    pending.accept(message.result);
                }
                return;
            }
            for (const listener of this.listeners.get(message.method) ?? []) {
                listener(message.params);
            }
        });
    }

    async send(method, params = {}) {
        await this.opened;
        const id = this.nextId++;
        const response = new Promise((accept, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`${method} timed out`));
            }, timeoutMs);
            this.pending.set(id, {
                accept: value => {
                    clearTimeout(timer);
                    accept(value);
                },
                reject: error => {
                    clearTimeout(timer);
                    reject(error);
                },
            });
        });
        this.socket.send(JSON.stringify({id, method, params}));
        return response;
    }

    on(method, listener) {
        const listeners = this.listeners.get(method) ?? [];
        listeners.push(listener);
        this.listeners.set(method, listeners);
    }

    async notify(method, params = {}) {
        await this.opened;
        this.socket.send(JSON.stringify({id: this.nextId++, method, params}));
    }

    close() {
        this.socket.close();
    }
}

async function evaluate(client, expression) {
    const result = await client.send("Runtime.evaluate", {
        expression,
        returnByValue: true,
        awaitPromise: true,
    });
    if (result.exceptionDetails) {
        throw new Error(result.exceptionDetails.exception?.description ?? result.exceptionDetails.text);
    }
    return result.result.value;
}

async function waitFor(client, description, predicate, errors) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (errors.length > 0) {
            throw new Error(errors.join("\n"));
        }
        const value = await predicate();
        if (value) {
            return value;
        }
        await new Promise(accept => setTimeout(accept, 50));
    }
    throw new Error(`timed out waiting for ${description}`);
}

function consoleText(argument) {
    if (argument.value !== undefined) {
        return String(argument.value);
    }
    return argument.description ?? argument.type;
}

function parseRect(value) {
    const values = value.split(",").map(Number);
    if (values.length !== 4 || values.some(number => !Number.isFinite(number))) {
        throw new Error(`invalid smoke-test rectangle: ${value}`);
    }
    return values;
}

async function dispatchClick(client, point) {
    await client.send("Input.dispatchMouseEvent", {type: "mouseMoved", ...point});
    await client.send("Input.dispatchMouseEvent", {type: "mousePressed", button: "left", clickCount: 1, ...point});
    await client.send("Input.dispatchMouseEvent", {type: "mouseReleased", button: "left", clickCount: 1, ...point});
}

async function runSmokeTest(client, url) {
    const errors = [];
    client.on("Runtime.exceptionThrown", event => {
        errors.push(event.exceptionDetails.exception?.description ?? event.exceptionDetails.text);
    });
    client.on("Runtime.consoleAPICalled", event => {
        if (event.type === "error" || event.type === "assert") {
            errors.push(event.args.map(consoleText).join(" "));
        }
    });
    client.on("Log.entryAdded", event => {
        if (event.entry.level === "error") {
            errors.push(event.entry.text);
        }
    });

    await Promise.all([
        client.send("Page.enable"),
        client.send("Runtime.enable"),
        client.send("Log.enable"),
    ]);
    await client.send("Page.navigate", {url});

    const ready = await waitFor(client, "the rendered UI", async () => {
        return evaluate(client, `(() => {
            const data = document.documentElement.dataset;
            if (data.feiStatus !== "ui text presented" || data.feiUiStatus !== "ready") return null;
            return {...data};
        })()`);
    }, errors);
    if (ready.feiFramePresented !== "true" || ready.feiFontAtlasUploaded !== "true") {
        throw new Error("the first frame or font atlas was not presented");
    }
    if (Number(ready.feiGlyphCount) <= 0 || Number(ready.feiGlyphBatches) <= 0) {
        throw new Error("the UI produced no glyphs");
    }
    console.log("browser smoke: rendered UI is ready");

    const geometry = await evaluate(client, `(() => {
        const canvas = document.getElementById("canvas");
        const bounds = canvas.getBoundingClientRect();
        return {
            canvas: {left: bounds.left, top: bounds.top, width: bounds.width, height: bounds.height},
            pixels: {width: canvas.width, height: canvas.height},
            data: {...document.documentElement.dataset},
        };
    })()`);
    const toViewportPoint = value => {
        const [x, y, width, height] = parseRect(value);
        return {
            x: geometry.canvas.left + (x + width / 2) * geometry.canvas.width / geometry.pixels.width,
            y: geometry.canvas.top + (y + height / 2) * geometry.canvas.height / geometry.pixels.height,
        };
    };

    await dispatchClick(client, toViewportPoint(geometry.data.feiUiButtonRect));
    await waitFor(client, "a UI button activation", async () => Number(await evaluate(client, "document.documentElement.dataset.feiUiClicks ?? 0")) >= 1, errors);
    console.log("browser smoke: button input passed");

    await dispatchClick(client, toViewportPoint(geometry.data.feiUiInputRect));
    await waitFor(client, "text input focus", async () => Number(await evaluate(client, "document.documentElement.dataset.feiUiFocus ?? -1")) >= 0, errors);
    for (const character of "Smoke") {
        const code = `Key${character.toUpperCase()}`;
        await evaluate(client, `window.dispatchEvent(new KeyboardEvent("keypress", ${JSON.stringify({key: character, code, bubbles: true})}))`);
    }
    await new Promise(accept => setTimeout(accept, 250));
    const typedText = await evaluate(client, "document.documentElement.dataset.feiUiText");
    if (typedText !== "Edit meSmoke") {
        throw new Error(`text input produced unexpected value ${JSON.stringify(typedText)}`);
    }
    console.log("browser smoke: text input passed");

    const scrollPoint = toViewportPoint(geometry.data.feiUiScrollRect);
    await client.send("Input.dispatchMouseEvent", {type: "mouseMoved", ...scrollPoint});
    await client.send("Input.dispatchMouseEvent", {type: "mouseWheel", deltaX: 0, deltaY: 160, ...scrollPoint});
    await waitFor(client, "UI scrolling", async () => Number(await evaluate(client, "document.documentElement.dataset.feiUiScrollY ?? 0")) !== 0, errors);
    console.log("browser smoke: scroll input passed");

    await new Promise(accept => setTimeout(accept, 250));
    if (errors.length > 0) {
        throw new Error(errors.join("\n"));
    }
    return evaluate(client, "({...document.documentElement.dataset})");
}

const server = await startServer();
const address = server.address();
const url = `http://127.0.0.1:${address.port}/sample-browser.html?smoke=${Date.now()}`;
const browserPath = findBrowser();
const profileDirectory = await mkdtemp(join(tmpdir(), "fei-browser-smoke-"));
const devToolsPort = await unusedPort();
const browserProcess = spawn(browserPath, [
    "--headless=new",
    "--enable-unsafe-webgpu",
    "--disable-gpu-sandbox",
    "--disable-background-mode",
    "--no-first-run",
    "--no-default-browser-check",
    `--remote-debugging-port=${devToolsPort}`,
    "--window-size=1280,720",
    `--user-data-dir=${profileDirectory}`,
    "about:blank",
], {stdio: ["ignore", "ignore", "pipe"]});

let browserClient;
let client;
try {
    console.log(`browser smoke: launching ${browserPath}`);
    const devToolsUrl = await waitForDevTools(browserProcess, devToolsPort);
    console.log("browser smoke: DevTools is ready");
    browserClient = new DevToolsClient(devToolsUrl);
    const pageTargetUrl = await waitForPageTarget(devToolsUrl);
    client = new DevToolsClient(pageTargetUrl);
    const result = await runSmokeTest(client, url);
    console.log(`browser smoke passed (${basename(browserPath)})`);
    console.log(`  glyphs: ${result.feiGlyphCount}, batches: ${result.feiGlyphBatches}`);
    console.log(`  clicks: ${result.feiUiClicks}, text: ${result.feiUiText}, scroll: ${result.feiUiScrollY}`);
} finally {
    await browserClient?.notify("Browser.close").catch(() => {});
    await new Promise(accept => setTimeout(accept, 250));
    client?.close();
    browserClient?.close();
    await stopBrowserProcesses(profileDirectory, browserProcess);
    if (browserProcess.exitCode === null) {
        const exited = new Promise(accept => browserProcess.once("exit", accept));
        browserProcess.kill();
        await Promise.race([
            exited,
            new Promise(accept => setTimeout(accept, 2000)),
        ]);
    }
    browserProcess.stderr.destroy();
    browserProcess.unref();
    const serverClosed = new Promise(accept => server.close(accept));
    server.closeAllConnections();
    await serverClosed;
    try {
        await rm(profileDirectory, {recursive: true, force: true, maxRetries: 3, retryDelay: 50});
    } catch (error) {
        console.warn(`failed to remove temporary browser profile: ${error.message}`);
    }
}
