"use strict";

const ui = {
  projectName: document.querySelector("#project-name"),
  runtimeDot: document.querySelector("#runtime-dot"),
  runtimeStatus: document.querySelector("#runtime-status"),
  runtimeFrame: document.querySelector("#runtime-frame"),
  restart: document.querySelector("#restart-runtime"),
  refreshFrame: document.querySelector("#refresh-frame"),
  frameStage: document.querySelector("#frame-stage"),
  gameFrame: document.querySelector("#game-frame"),
  framePlaceholder: document.querySelector("#frame-placeholder"),
  frameDimensions: document.querySelector("#frame-dimensions"),
  frameMessage: document.querySelector("#frame-message"),
  actionOverlay: document.querySelector("#action-overlay"),
  activityTitle: document.querySelector("#activity-title"),
  activityState: document.querySelector("#activity-state"),
  activitySummary: document.querySelector("#activity-summary"),
  activityMeta: document.querySelector("#activity-meta"),
  timeline: document.querySelector("#timeline"),
  eventCount: document.querySelector("#event-count"),
  detailPanel: document.querySelector("#detail-panel"),
  detailTitle: document.querySelector("#detail-title"),
  detailSequence: document.querySelector("#detail-sequence"),
  detailJson: document.querySelector("#detail-json"),
  followLive: document.querySelector("#follow-live"),
  closeDetails: document.querySelector("#close-details"),
  filters: [...document.querySelectorAll(".filter")],
  mobileTabs: [...document.querySelectorAll(".mobile-tab")],
  mobilePanels: [...document.querySelectorAll("[data-mobile-panel]")],
};

const actionableCalls = new Set(["step", "observe", "capture"]);

const state = {
  cursor: 0,
  events: [],
  starts: new Map(),
  finishedSpans: new Set(),
  observations: new Map(),
  selectedSequence: null,
  followLive: true,
  filter: "all",
  frameUrl: null,
  frameLoading: false,
  frameDirty: false,
  runtimeConnected: false,
  runtimeKey: null,
  tracesWithCalls: new Set(),
  lastActivityEvent: null,
  mobileView: "game",
};

function pretty(value) {
  if (value === undefined) return "—";
  if (typeof value === "string") return value;
  return JSON.stringify(value, null, 2);
}

function oneLine(value) {
  return pretty(value).replaceAll("\n", " ");
}

function inspectionPayload(response) {
  return response && response.ok === true ? response.payload : null;
}

function requestFor(event) {
  return state.starts.get(event.span_id)?.data?.request ?? null;
}

function interfaceFor(event) {
  return requestFor(event)?.interface ?? event.data?.request?.interface ?? "—";
}

function actionSummary(event) {
  const request = requestFor(event) ?? event.data?.request;
  if (!request) return "";
  if (request.action !== undefined) return oneLine(request.action);
  if (request.interface) return request.interface;
  return "";
}

function jsonDiff(before, after, path = "$") {
  if (Object.is(before, after)) return [];
  if (before === null || after === null || typeof before !== "object" || typeof after !== "object") {
    return [{ path, before, after }];
  }
  const keys = new Set([...Object.keys(before), ...Object.keys(after)]);
  const changes = [];
  for (const key of [...keys].sort()) {
    changes.push(...jsonDiff(before[key], after[key], `${path}.${key}`));
    if (changes.length >= 100) break;
  }
  return changes;
}

function processObservation(event) {
  if (event.type !== "span.completed" || !["step", "observe"].includes(event.name)) return;
  const payload = inspectionPayload(event.data?.response);
  if (!payload || payload.observation === undefined) return;
  const interfaceId = payload.interface ?? interfaceFor(event);
  const before = state.observations.get(interfaceId);
  event.observation_diff = before === undefined ? [] : jsonDiff(before, payload.observation);
  state.observations.set(interfaceId, payload.observation);
}

function setActivityState(value) {
  ui.activityState.className = `state-pill ${value}`;
  ui.activityState.textContent = value;
}

function activitySummary(event, started, failed, request, response) {
  if (event.name === "step" && request?.action !== undefined) return oneLine(request.action);
  if (failed) return response?.error?.message ?? response?.message ?? oneLine(response ?? event.data);
  if (started && event.name === "play-run") return "Executing dynamic Luau playtest script";
  if (event.name === "observe") return started ? "Reading game state" : "Observation received";
  if (event.name === "capture") return started ? "Capturing game frame" : "Frame captured";
  if (event.name === "play-run") return "Play script finished";
  return event.type;
}

function updateActivity(event) {
  const started = event.type === "span.started" || event.type === "play_run.started";
  const failed = event.type.endsWith("failed");
  const request = requestFor(event) ?? event.data?.request;
  const response = event.data?.response ?? event.data?.report;
  const payload = inspectionPayload(response);
  const interfaceId = request?.interface ?? payload?.interface;
  const ticks = request?.ticks ?? payload?.ticks;
  const meta = [ticks === undefined ? null : `${ticks} ticks`,
    event.duration_ms === undefined ? null : `${event.duration_ms} ms`].filter(Boolean);

  ui.activityTitle.textContent = event.name === "play-run"
    ? "Play script"
    : `${event.name}${interfaceId ? ` · ${interfaceId}` : ""}`;
  ui.activitySummary.textContent = activitySummary(event, started, failed, request, response);
  ui.activityMeta.textContent = meta.join(" · ") || "—";
  setActivityState(started ? "running" : failed ? "failed" : "completed");
  state.lastActivityEvent = event;

  if (event.name === "step" && request?.action !== undefined) {
    ui.actionOverlay.hidden = false;
    ui.actionOverlay.textContent = `step ${request.interface ?? ""}  ${oneLine(request.action)}`;
  }
}

function processEvent(event) {
  if (event.type === "span.started") state.starts.set(event.span_id, event);
  if (actionableCalls.has(event.name)) state.tracesWithCalls.add(event.trace_id);

  if (event.type === "span.completed" || event.type === "span.failed") {
    state.finishedSpans.add(event.span_id);
    processObservation(event);
    if (actionableCalls.has(event.name)) updateActivity(event);
    if (event.name === "step" && event.type === "span.completed") requestFrame();
  } else if (event.type === "play_run.started") {
    updateActivity(event);
  } else if (event.type === "play_run.completed" || event.type === "play_run.failed") {
    if (state.tracesWithCalls.has(event.trace_id)) {
      setActivityState(event.type.endsWith("failed") ? "failed" : "completed");
    } else {
      updateActivity(event);
    }
  } else if (event.type === "span.started" && actionableCalls.has(event.name)) {
    updateActivity(event);
  }

  followEvent(event);
}

function eventStatus(event) {
  if (event.type.endsWith("failed")) return { icon: "×", className: "failed" };
  if (event.type.endsWith("started")) return { icon: "●", className: "running" };
  if (event.type === "play_log") return { icon: "•", className: "running" };
  if (event.type === "restart.requested") return { icon: "↻", className: "running" };
  return { icon: "✓", className: "ok" };
}

function isVisibleEvent(event) {
  if (event.type === "span.started" && state.finishedSpans.has(event.span_id)) return false;
  if (event.type === "play_run.started" && state.events.some((item) =>
    item.span_id === event.span_id && (item.type === "play_run.completed" || item.type === "play_run.failed"))) return false;
  if (state.filter === "errors") return event.type.endsWith("failed");
  if (state.filter === "actions") return actionableCalls.has(event.name);
  return true;
}

function detailFor(event) {
  const detail = {
    type: event.type,
    trace_id: event.trace_id,
    span_id: event.span_id,
    parent_span_id: event.parent_span_id,
    timestamp: new Date(event.timestamp_ms).toISOString(),
  };
  const request = requestFor(event) ?? event.data?.request;
  if (request !== undefined) detail.request = request;
  if (event.data?.response !== undefined) detail.response = event.data.response;
  if (event.data?.report !== undefined) detail.report = event.data.report;
  if (event.type === "play_run.started") detail.script = event.data?.source;
  if (event.type === "play_log") detail.log = event.data;
  if (event.observation_diff?.length) detail.observation_diff = event.observation_diff;
  if (event.duration_ms !== undefined) detail.duration_ms = event.duration_ms;
  if (event.data?.truncated) detail.truncated = event.data;
  return detail;
}

function isNarrowLayout() {
  return window.matchMedia("(max-width: 800px)").matches;
}

function setMobileView(view) {
  state.mobileView = view;
  for (const tab of ui.mobileTabs) tab.classList.toggle("active", tab.dataset.mobileView === view);
  for (const panel of ui.mobilePanels) panel.classList.toggle("mobile-active", panel.dataset.mobilePanel === view);
}

function openInspector() {
  ui.detailPanel.classList.add("open");
  if (isNarrowLayout()) setMobileView("details");
}

function closeInspector() {
  ui.detailPanel.classList.remove("open");
  if (isNarrowLayout()) setMobileView("game");
}

function showDetail(event, follow) {
  state.followLive = follow;
  state.selectedSequence = follow ? null : event.sequence;
  ui.followLive.classList.toggle("active", follow);
  ui.detailTitle.textContent = event.type === "play_log" ? "play.log" : event.name;
  ui.detailSequence.textContent = `#${event.sequence} · ${new Date(event.timestamp_ms).toLocaleTimeString([], { hour12: false })}`;
  ui.detailJson.textContent = pretty(detailFor(event));
}

function selectEvent(event) {
  showDetail(event, false);
  openInspector();
  renderTimeline();
}

function followEvent(event) {
  if (state.followLive) showDetail(event, true);
}

function renderTimeline() {
  const visible = state.events.filter(isVisibleEvent).slice(-500).reverse();
  ui.eventCount.textContent = `${visible.length} event${visible.length === 1 ? "" : "s"}`;
  if (!visible.length) {
    const empty = document.createElement("div");
    empty.className = "empty-state";
    empty.textContent = state.filter === "all" ? "Waiting for agent activity…" : "No matching events";
    ui.timeline.replaceChildren(empty);
    return;
  }
  const rows = visible.map((event) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `event-row${state.selectedSequence === event.sequence ? " selected" : ""}`;
    const status = eventStatus(event);
    const icon = document.createElement("span");
    icon.className = `event-icon ${status.className}`;
    icon.textContent = status.icon;
    const main = document.createElement("span");
    main.className = "event-main";
    const titleLine = document.createElement("span");
    titleLine.className = "event-title-line";
    const name = document.createElement("span");
    name.className = "event-name";
    name.textContent = event.type === "play_log" ? "play.log" : event.name;
    const time = document.createElement("span");
    time.className = "event-time";
    time.textContent = new Date(event.timestamp_ms).toLocaleTimeString([], { hour12: false });
    titleLine.append(name, time);
    const summary = document.createElement("span");
    summary.className = "event-summary";
    summary.textContent = event.type === "play_log" ? oneLine(event.data) : actionSummary(event);
    main.append(titleLine, summary);
    const duration = document.createElement("span");
    duration.className = "event-duration";
    duration.textContent = event.duration_ms === undefined ? "" : `${event.duration_ms} ms`;
    button.append(icon, main, duration);
    button.addEventListener("click", () => selectEvent(event));
    return button;
  });
  ui.timeline.replaceChildren(...rows);
}

async function pollEvents() {
  while (true) {
    try {
      const response = await fetch(`/api/v1/play/events?after=${state.cursor}&limit=500&timeout_ms=25000`, { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const batch = await response.json();
      if (batch.gap) {
        state.events = [];
        state.starts.clear();
        state.finishedSpans.clear();
        state.observations.clear();
        state.tracesWithCalls.clear();
        state.lastActivityEvent = null;
      }
      for (const event of batch.events) {
        processEvent(event);
        state.events.push(event);
      }
      state.events = state.events.slice(-1000);
      state.cursor = batch.cursor ?? batch.latest;
      if (batch.events.length) renderTimeline();
    } catch (error) {
      ui.frameMessage.textContent = `Trace stream unavailable: ${error.message}`;
      await new Promise((resolve) => setTimeout(resolve, 1000));
    }
  }
}

async function requestFrame() {
  if (state.frameLoading) {
    state.frameDirty = true;
    return;
  }
  state.frameLoading = true;
  ui.frameMessage.textContent = "Loading frame…";
  try {
    const response = await fetch("/api/v1/play/frame", { cache: "no-store" });
    if (!response.ok) {
      const error = await response.json().catch(() => ({}));
      throw new Error(error.message ?? `HTTP ${response.status}`);
    }
    const blob = await response.blob();
    const nextUrl = URL.createObjectURL(blob);
    ui.gameFrame.src = nextUrl;
    ui.gameFrame.style.display = "block";
    ui.framePlaceholder.hidden = true;
    if (state.frameUrl) URL.revokeObjectURL(state.frameUrl);
    state.frameUrl = nextUrl;
    const width = response.headers.get("X-Entisium-Width") ?? "—";
    const height = response.headers.get("X-Entisium-Height") ?? "—";
    const frame = response.headers.get("X-Entisium-Frame") ?? "—";
    ui.frameDimensions.textContent = `${width} × ${height}`;
    ui.frameMessage.textContent = `Runtime frame ${frame}`;
  } catch (error) {
    ui.frameMessage.textContent = `Frame unavailable: ${error.message}`;
  } finally {
    state.frameLoading = false;
    if (state.frameDirty) {
      state.frameDirty = false;
      requestFrame();
    }
  }
}

async function refreshStatus() {
  try {
    const response = await fetch("/api/v1/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const status = await response.json();
    ui.projectName.textContent = status.project?.name ?? "Unnamed project";
    const connection = status.runtime?.connection ?? "waiting";
    ui.runtimeStatus.textContent = connection;
    ui.runtimeDot.className = `status-dot ${connection}`;
    ui.runtimeFrame.textContent = status.runtime?.frame == null ? "frame —" : `frame ${status.runtime.frame}`;
    const connected = connection === "connected";
    ui.frameStage.classList.toggle("offline", !connected);
    const runtimeKey = connected ? `${status.session}:${status.runtime?.reported_process_id ?? "runtime"}` : null;
    if (connected && (!state.runtimeConnected || state.runtimeKey !== runtimeKey)) requestFrame();
    state.runtimeConnected = connected;
    state.runtimeKey = runtimeKey;
  } catch (error) {
    ui.runtimeStatus.textContent = "agentd unavailable";
    ui.runtimeDot.className = "status-dot disconnected";
    ui.frameStage.classList.add("offline");
  }
}

ui.restart.addEventListener("click", async () => {
  ui.restart.disabled = true;
  try {
    const response = await fetch("/api/v1/restart", { method: "POST", cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    ui.frameMessage.textContent = "Runtime restart requested";
    ui.frameStage.classList.add("offline");
  } catch (error) {
    ui.frameMessage.textContent = `Restart failed: ${error.message}`;
  } finally {
    setTimeout(() => { ui.restart.disabled = false; }, 800);
  }
});

ui.refreshFrame.addEventListener("click", requestFrame);
ui.closeDetails.addEventListener("click", closeInspector);
ui.followLive.addEventListener("click", () => {
  state.followLive = true;
  state.selectedSequence = null;
  ui.followLive.classList.add("active");
  const latest = [...state.events].reverse().find(isVisibleEvent) ?? state.events.at(-1);
  if (latest) showDetail(latest, true);
  renderTimeline();
});

for (const button of ui.filters) {
  button.addEventListener("click", () => {
    state.filter = button.dataset.filter;
    for (const item of ui.filters) item.classList.toggle("active", item === button);
    renderTimeline();
  });
}

for (const button of ui.mobileTabs) {
  button.addEventListener("click", () => setMobileView(button.dataset.mobileView));
}

setMobileView("game");
refreshStatus();
setInterval(refreshStatus, 1000);
pollEvents();
