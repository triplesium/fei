const elements = {
  capabilityCount: document.querySelector("#capability-count"),
  capabilityNav: document.querySelector("#capability-nav"),
  connectionState: document.querySelector("#connection-state"),
  content: document.querySelector("#content"),
  main: document.querySelector("#main-content"),
  protocolVersion: document.querySelector("#protocol-version"),
  refresh: document.querySelector("#refresh-devtools"),
  search: document.querySelector("#capability-search"),
  serviceName: document.querySelector("#service-name"),
  sidebar: document.querySelector("#sidebar"),
  sidebarBlobToggle: document.querySelector("#toggle-blobs"),
  sidebarToggle: document.querySelector("#sidebar-toggle"),
  statusSummary: document.querySelector("#status-summary"),
  toastRegion: document.querySelector("#toast-region"),
};

const state = {
  capabilityState: new Map(),
  collapsedSidebarGroups: new Set(),
  discovery: null,
  inlineBlobIds: new Set(),
  manifest: null,
  schemas: null,
  selectedId: null,
  showSidebarBlobs: loadShowSidebarBlobs(),
  status: null,
};

const supportedProtocolVersion = 1;
const maximumCollectionRows = 200;
const maximumPreviewBytes = 2 * 1024 * 1024;
const sidebarShowBlobsStorageKey = "fei-devtools-sidebar-show-blobs";

function loadShowSidebarBlobs() {
  try {
    return window.localStorage.getItem("fei-devtools-sidebar-show-blobs") === "true";
  } catch {
    return false;
  }
}

class DevToolsError extends Error {
  constructor(message, details = {}) {
    super(message);
    this.name = "DevToolsError";
    this.status = details.status ?? null;
    this.capability = details.capability ?? "";
    this.payload = details.payload ?? null;
  }
}

function createElement(tag, options = {}, ...children) {
  const element = document.createElement(tag);
  if (options.className) {
    element.className = options.className;
  }
  if (options.text !== undefined) {
    element.textContent = String(options.text);
  }
  if (options.type) {
    element.type = options.type;
  }
  if (options.title) {
    element.title = options.title;
  }
  if (options.value !== undefined) {
    element.value = options.value;
  }
  if (options.href) {
    element.href = options.href;
  }
  for (const [name, value] of Object.entries(options.attributes ?? {})) {
    if (value !== null && value !== undefined) {
      element.setAttribute(name, String(value));
    }
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined) {
      continue;
    }
    element.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return element;
}

function replaceChildren(parent, ...children) {
  parent.replaceChildren(...children.filter((child) => child !== null && child !== undefined));
}

function safeStringify(value) {
  const encoded = JSON.stringify(value, null, 2);
  return encoded === undefined ? String(value) : encoded;
}

function displayLabel(name) {
  if (!name) {
    return "Value";
  }
  const spaced = String(name)
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .replace(/[._-]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  return spaced.replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function timestampLabel(date = new Date()) {
  return new Intl.DateTimeFormat(undefined, {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(date);
}

function showToast(message, kind = "info") {
  const toast = createElement("div", { className: `toast ${kind}`, text: message });
  elements.toastRegion.append(toast);
  window.setTimeout(() => toast.remove(), 4500);
}

function setConnection(kind, label) {
  elements.connectionState.className = `connection-state ${kind}`;
  const labelNode = elements.connectionState.querySelector("span:last-child");
  labelNode.textContent = label;
}

function sameOriginUrl(path, params = {}) {
  const url = new URL(path, window.location.origin);
  if (url.origin !== window.location.origin) {
    throw new DevToolsError(`Cross-origin endpoint rejected: ${url.href}`);
  }
  for (const [name, value] of Object.entries(params)) {
    if (value !== undefined && value !== null) {
      url.searchParams.set(name, String(value));
    }
  }
  return url;
}

async function readErrorResponse(response) {
  const text = await response.text();
  let payload = null;
  try {
    payload = text ? JSON.parse(text) : null;
  } catch {
    payload = null;
  }
  return new DevToolsError(
    payload?.message || text || `${response.status} ${response.statusText}`,
    {
      status: response.status,
      capability: payload?.capability,
      payload,
    },
  );
}

async function fetchJson(path, options = {}) {
  const response = await fetch(sameOriginUrl(path, options.params), {
    cache: "no-store",
    redirect: "error",
    ...options.fetch,
  });
  if (!response.ok) {
    throw await readErrorResponse(response);
  }
  const text = await response.text();
  try {
    return text ? JSON.parse(text) : null;
  } catch {
    throw new DevToolsError(`Invalid JSON returned by ${path}`, {
      status: response.status,
      payload: text,
    });
  }
}

function validateDiscovery(discovery) {
  if (!discovery || typeof discovery !== "object") {
    throw new DevToolsError("Discovery response must be an object");
  }
  if (discovery.version !== supportedProtocolVersion) {
    throw new DevToolsError(
      `Unsupported DevTools protocol version: ${String(discovery.version)}`,
    );
  }
  for (const field of ["manifest", "schemas", "status"]) {
    if (typeof discovery[field] !== "string" || !discovery[field]) {
      throw new DevToolsError(`Discovery response is missing '${field}'`);
    }
  }
  return discovery;
}

function validateManifest(manifest) {
  if (
    !manifest ||
    typeof manifest !== "object" ||
    manifest.version !== supportedProtocolVersion ||
    !Array.isArray(manifest.capabilities)
  ) {
    throw new DevToolsError("Manifest response has an unsupported shape");
  }

  const seen = new Set();
  for (const capability of manifest.capabilities) {
    if (
      !capability ||
      typeof capability.id !== "string" ||
      !capability.id ||
      !Array.isArray(capability.endpoints) ||
      capability.endpoints.some(
        (endpoint) =>
          !endpoint ||
          typeof endpoint.rel !== "string" ||
          typeof endpoint.method !== "string" ||
          typeof endpoint.path !== "string",
      )
    ) {
      throw new DevToolsError("Manifest contains an invalid capability entry");
    }
    if (seen.has(capability.id)) {
      throw new DevToolsError(`Manifest contains duplicate capability '${capability.id}'`);
    }
    seen.add(capability.id);
  }
  return manifest;
}

function validateSchemas(schemas) {
  if (
    !schemas ||
    typeof schemas !== "object" ||
    schemas.version !== supportedProtocolVersion ||
    !schemas.types ||
    typeof schemas.types !== "object" ||
    Array.isArray(schemas.types)
  ) {
    throw new DevToolsError("Schema response has an unsupported shape");
  }
  return schemas;
}

function getCapabilityState(id) {
  if (!state.capabilityState.has(id)) {
    state.capabilityState.set(id, {
      error: null,
      loading: false,
      payload: undefined,
      responseType: "",
      updatedAt: null,
      viewMode: "auto",
      blob: null,
      blobUrl: null,
      dataQuery: "",
    });
  }
  return state.capabilityState.get(id);
}

function releaseBlobUrl(capabilityState) {
  if (capabilityState?.blobUrl) {
    URL.revokeObjectURL(capabilityState.blobUrl);
    capabilityState.blobUrl = null;
  }
}

function ensureBlobUrl(capabilityState) {
  if (!capabilityState?.blob) {
    return null;
  }
  if (!capabilityState.blobUrl) {
    capabilityState.blobUrl = URL.createObjectURL(capabilityState.blob.blob);
  }
  return capabilityState.blobUrl;
}

function cleanupRenderedBlobs(nextId) {
  for (const id of state.inlineBlobIds) {
    if (id !== nextId) {
      releaseBlobUrl(state.capabilityState.get(id));
    }
  }
  state.inlineBlobIds.clear();
  if (state.selectedId && state.selectedId !== nextId) {
    releaseBlobUrl(state.capabilityState.get(state.selectedId));
  }
}

function capabilityFromHash() {
  const prefix = "#/capability/";
  if (!window.location.hash.startsWith(prefix)) {
    return null;
  }
  try {
    return decodeURIComponent(window.location.hash.slice(prefix.length));
  } catch {
    return null;
  }
}

function navigateToCapability(id) {
  window.location.hash = `#/capability/${encodeURIComponent(id)}`;
}

function selectedCapability() {
  return state.manifest?.capabilities.find((item) => item.id === state.selectedId) ?? null;
}

function capabilityById(id) {
  return state.manifest?.capabilities.find((item) => item.id === id) ?? null;
}

function endpointByRel(capability, rel) {
  return capability?.endpoints?.find((endpoint) => endpoint.rel === rel) ?? null;
}

function isBlobCapability(capability) {
  return endpointByRel(capability, "read") !== null;
}

function updateStatusSummary() {
  if (!state.status) {
    elements.statusSummary.textContent = "";
    return;
  }
  const capabilities = state.status.capabilities ?? state.manifest?.capabilities.length ?? 0;
  const pending = state.status.pending_requests ?? 0;
  elements.statusSummary.textContent = `${capabilities} capabilities · ${pending} pending`;
}

function setSidebarOpen(open) {
  elements.sidebar.classList.toggle("open", open);
  elements.sidebarToggle.setAttribute("aria-expanded", String(open));
}

function capabilityNamespace(id) {
  const separator = id.indexOf(".");
  return separator > 0 ? id.slice(0, separator) : "other";
}

function updateSidebarGroupingControls() {
  elements.sidebarBlobToggle.classList.toggle("active", state.showSidebarBlobs);
  elements.sidebarBlobToggle.setAttribute(
    "aria-pressed",
    String(state.showSidebarBlobs),
  );
  elements.sidebarBlobToggle.title = state.showSidebarBlobs
    ? "Hide blob capabilities"
    : "Show blob capabilities";
}

function setShowSidebarBlobs(show) {
  state.showSidebarBlobs = show;
  try {
    window.localStorage.setItem(sidebarShowBlobsStorageKey, String(show));
  } catch {
    // The visibility preference remains available for the current page session.
  }
  renderSidebar();
}

function renderSidebar() {
  const query = elements.search.value.trim().toLowerCase();
  const capabilities = state.manifest?.capabilities ?? [];
  const sidebarCapabilities = state.showSidebarBlobs
    ? capabilities
    : capabilities.filter((capability) => !isBlobCapability(capability));
  const grouped = new Map();

  for (const capability of sidebarCapabilities) {
    const searchable = `${capability.label ?? ""} ${capability.id}`.toLowerCase();
    if (query && !searchable.includes(query)) {
      continue;
    }
    const group = capabilityNamespace(capability.id);
    if (!grouped.has(group)) {
      grouped.set(group, []);
    }
    grouped.get(group).push(capability);
  }

  const groups = [];
  const groupedEntries = [...grouped.entries()];
  groupedEntries.sort(([left], [right]) => {
    if (left === "other") {
      return 1;
    }
    if (right === "other") {
      return -1;
    }
    return left.localeCompare(right);
  });
  for (const [groupName, items] of groupedEntries) {
    if (!items.length) {
      continue;
    }
    items.sort((left, right) =>
      (left.label || left.id).localeCompare(right.label || right.id),
    );
    const collapsedGroups = state.collapsedSidebarGroups;
    const group = createElement("details", {
      className: "nav-group",
      attributes: collapsedGroups.has(groupName) && !query ? {} : { open: "" },
    });
    const groupLabel =
      groupName === "other" ? "Other" : displayLabel(groupName);
    const title = createElement(
      "summary",
      { className: "nav-group-title" },
      createElement("span", {
        className: "nav-group-chevron",
        text: "›",
        attributes: { "aria-hidden": "true" },
      }),
      createElement("span", { className: "nav-group-label", text: groupLabel }),
      createElement("span", { className: "nav-group-count", text: items.length }),
    );
    const itemList = createElement("div", { className: "nav-group-items" });
    for (const capability of items) {
      const label = capability.label || displayLabel(capability.id);
      const button = createElement(
        "button",
        {
          className: `nav-item${capability.id === state.selectedId ? " active" : ""}`,
          type: "button",
          attributes: {
            "aria-label": `${label} — ${capability.id}`,
            "data-capability-id": capability.id,
            title: capability.id,
          },
        },
        createElement("span", { text: label }),
      );
      button.addEventListener("click", () => {
        navigateToCapability(capability.id);
        setSidebarOpen(false);
      });
      itemList.append(button);
    }
    group.append(title, itemList);
    group.addEventListener("toggle", () => {
      if (group.open) {
        collapsedGroups.delete(groupName);
      } else {
        collapsedGroups.add(groupName);
      }
    });
    groups.push(group);
  }

  if (!groups.length) {
    groups.push(
      createElement("div", {
        className: "nav-empty",
        text: query ? "No matching capabilities" : "No capabilities exposed",
      }),
    );
  }
  replaceChildren(elements.capabilityNav, ...groups);
  updateSidebarGroupingControls();
  const hiddenBlobCount = capabilities.length - sidebarCapabilities.length;
  elements.capabilityCount.textContent =
    state.showSidebarBlobs || hiddenBlobCount === 0
      ? `${capabilities.length} ${capabilities.length === 1 ? "capability" : "capabilities"}`
      : `${sidebarCapabilities.length} of ${capabilities.length}`;
  elements.capabilityCount.title = hiddenBlobCount
    ? `${hiddenBlobCount} blob ${
        hiddenBlobCount === 1 ? "capability" : "capabilities"
      } hidden`
    : "";
}

function chip(value) {
  return createElement("span", { className: "chip", text: value });
}

function capabilityHeader(capability) {
  const actions = createElement("div", { className: "header-actions" });
  const status = createElement("span", { className: "last-updated", text: "Ready" });
  const header = createElement(
    "header",
    { className: "capability-header" },
    createElement(
      "div",
      { className: "capability-header-main" },
      createElement(
        "div",
        { className: "capability-identity" },
        createElement(
          "div",
          { className: "capability-heading" },
          createElement("h1", { text: capability.label || displayLabel(capability.id) }),
        ),
        createElement("code", { className: "capability-id", text: capability.id }),
      ),
      createElement("div", { className: "capability-controls" }, status, actions),
    ),
  );

  const detailRows = [
    ["Mode", capability.mode],
    ["Schema", capability.schema],
    ["Request type", capability.request_type],
    ["Response type", capability.response_type],
    ["MIME", capability.mime],
  ].filter(([, value]) => value);
  if (detailRows.length) {
    header.append(
      createElement(
        "details",
        { className: "capability-details" },
        createElement("summary", { text: "Details" }),
        createElement(
          "dl",
          {},
          detailRows.flatMap(([label, value]) => [
            createElement("dt", { text: label }),
            createElement("dd", {}, createElement("code", { text: value })),
          ]),
        ),
      ),
    );
  }
  return { actions, element: header, status };
}

function loadingInline(label) {
  return createElement(
    "div",
    { className: "loading-inline" },
    createElement("span", { className: "spinner", attributes: { "aria-hidden": "true" } }),
    createElement("span", { text: label }),
  );
}

function requestError(error, retry = null) {
  const box = createElement(
    "div",
    { className: "request-error", attributes: { role: "alert" } },
    createElement("strong", { text: error?.message || "Request failed" }),
  );
  const details = [];
  for (const [label, value] of [
    ["Status", error?.status],
    ["Capability", error?.capability],
  ]) {
    if (value !== null && value !== undefined && value !== "") {
      details.push(createElement("dt", { text: label }), createElement("dd", { text: value }));
    }
  }
  if (details.length) {
    box.append(createElement("dl", {}, details));
  }
  if (retry) {
    const button = createElement("button", {
      className: "button ghost",
      type: "button",
      text: "Retry",
    });
    button.addEventListener("click", retry);
    box.append(createElement("div", { className: "form-actions" }, button));
  }
  return box;
}

function getType(typeName) {
  return typeName ? state.schemas?.types?.[typeName] ?? null : null;
}

function primitiveNode(value, kind = "") {
  if (value === null || value === undefined) {
    return createElement("span", { className: "null-value", text: "null" });
  }
  const valueKind =
    kind === "bool" || typeof value === "boolean"
      ? "boolean"
      : typeof value === "number"
        ? "number"
        : "string";
  return createElement("span", {
    className: `primitive-value ${valueKind}`,
    text: typeof value === "string" ? value : String(value),
  });
}

function jsonTree(value, depth = 0) {
  const container = createElement("div", { className: depth === 0 ? "json-tree" : "" });
  if (value === null || typeof value !== "object") {
    container.append(primitiveNode(value));
    return container;
  }

  const entries = Array.isArray(value)
    ? value.map((item, index) => [String(index), item])
    : Object.entries(value);
  const details = createElement("details", { attributes: { open: depth === 0 ? "" : null } });
  const label = Array.isArray(value)
    ? `${entries.length} items`
    : `${entries.length} fields`;
  details.append(createElement("summary", { text: label }));
  for (const [key, entryValue] of entries) {
    if (entryValue !== null && typeof entryValue === "object") {
      const nested = jsonTree(entryValue, depth + 1);
      const wrapper = createElement(
        "div",
        { className: "json-entry" },
        createElement("span", { className: "json-key", text: key }),
        nested,
      );
      details.append(wrapper);
    } else {
      details.append(
        createElement(
          "div",
          { className: "json-entry" },
          createElement("span", { className: "json-key", text: key }),
          primitiveNode(entryValue),
        ),
      );
    }
  }
  container.append(details);
  return container;
}

function isPrimitiveSchema(schema) {
  return [
    "bool",
    "string",
    "signed_integer",
    "unsigned_integer",
    "floating",
    "enum",
  ].includes(schema?.kind);
}

function isFlatTableType(typeName) {
  const schema = getType(typeName);
  if (schema?.kind !== "object" || !Array.isArray(schema.properties)) {
    return false;
  }
  return schema.properties.every((property) => {
    const propertySchema = getType(property.type);
    if (isPrimitiveSchema(propertySchema)) {
      return true;
    }
    if (propertySchema?.kind === "optional") {
      return isPrimitiveSchema(getType(propertySchema.element_type));
    }
    return false;
  });
}

function collectionElementType(schema) {
  return schema.element_type ?? schema.key_type ?? "";
}

function tableColumnClass(typeName) {
  let schema = getType(typeName);
  while (schema?.kind === "optional") {
    schema = getType(schema.element_type);
  }
  if (["signed_integer", "unsigned_integer", "floating"].includes(schema?.kind)) {
    return "data-table-column data-table-column-number";
  }
  if (schema?.kind === "string") {
    return "data-table-column data-table-column-text";
  }
  if (["bool", "enum"].includes(schema?.kind)) {
    return "data-table-column data-table-column-compact";
  }
  return "data-table-column";
}

function renderFlatTable(values, elementType) {
  const schema = getType(elementType);
  const properties = schema.properties ?? [];
  const table = createElement("table", { className: "data-table" });
  const header = createElement("tr");
  for (const property of properties) {
    header.append(
      createElement("th", {
        className: tableColumnClass(property.type),
        text: displayLabel(property.name),
      }),
    );
  }
  table.append(createElement("thead", {}, header));
  const body = createElement("tbody");
  for (const value of values.slice(0, maximumCollectionRows)) {
    const row = createElement("tr");
    for (const property of properties) {
      const cell = createElement("td", { className: tableColumnClass(property.type) });
      cell.append(renderAuto(value?.[property.name], property.type, 1));
      row.append(cell);
    }
    body.append(row);
  }
  table.append(body);
  const wrapper = createElement("div", { className: "data-table-wrap" }, table);
  if (values.length > maximumCollectionRows) {
    wrapper.append(
      createElement("p", {
        className: "muted",
        text: `Showing ${maximumCollectionRows} of ${values.length} rows`,
      }),
    );
  }
  return wrapper;
}

function renderCollection(value, schema, depth) {
  if (!Array.isArray(value)) {
    return jsonTree(value);
  }
  if (!value.length) {
    return createElement("span", { className: "collection-summary", text: "0 items" });
  }
  const elementType = collectionElementType(schema);
  let content;
  if (isFlatTableType(elementType)) {
    content = renderFlatTable(value, elementType);
  } else {
    const list = createElement("ol", { className: "collection-list" });
    for (const [index, item] of value.slice(0, maximumCollectionRows).entries()) {
      list.append(
        createElement(
          "li",
          { className: "collection-item" },
          createElement("span", { className: "collection-index", text: `#${index}` }),
          renderAuto(item, elementType, depth + 1),
        ),
      );
    }
    if (value.length > maximumCollectionRows) {
      list.append(
        createElement("li", {
          className: "collection-item muted",
          text: `${value.length - maximumCollectionRows} additional items hidden`,
        }),
      );
    }
    content = list;
  }
  const details = createElement("details", { className: "collection-disclosure" });
  details.append(createElement("summary", { text: `${value.length} items` }), content);
  return details;
}

function renderMap(value, schema, depth) {
  if (!Array.isArray(value)) {
    return jsonTree(value);
  }
  if (!value.length) {
    return createElement("span", { className: "collection-summary", text: "0 entries" });
  }
  const table = createElement("table", { className: "data-table" });
  table.append(
    createElement(
      "thead",
      {},
      createElement(
        "tr",
        {},
        createElement("th", { className: tableColumnClass(schema.key_type), text: "Key" }),
        createElement("th", {
          className: tableColumnClass(schema.mapped_type),
          text: "Value",
        }),
      ),
    ),
  );
  const body = createElement("tbody");
  for (const entry of value.slice(0, maximumCollectionRows)) {
    body.append(
      createElement(
        "tr",
        {},
        createElement(
          "td",
          { className: tableColumnClass(schema.key_type) },
          renderAuto(entry?.key, schema.key_type, depth + 1),
        ),
        createElement(
          "td",
          { className: tableColumnClass(schema.mapped_type) },
          renderAuto(entry?.value, schema.mapped_type, depth + 1),
        ),
      ),
    );
  }
  table.append(body);
  const details = createElement("details", { className: "collection-disclosure" });
  details.append(
    createElement("summary", { text: `${value.length} entries` }),
    createElement("div", { className: "data-table-wrap" }, table),
  );
  return details;
}

function renderObject(value, schema, depth) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    return jsonTree(value);
  }
  const grid = createElement("div", { className: "field-grid" });
  for (const property of schema.properties ?? []) {
    const fieldName = createElement(
      "div",
      {
        className: "field-name",
        text: displayLabel(property.name),
        title: property.name,
      },
    );
    grid.append(
      createElement(
        "div",
        {
          className: "field-row",
          attributes: { "data-search-alias": property.name },
        },
        fieldName,
        createElement(
          "div",
          { className: "field-value" },
          renderAuto(value[property.name], property.type, depth + 1),
        ),
      ),
    );
  }
  const unknownProperties = Object.keys(value).filter(
    (key) => !(schema.properties ?? []).some((property) => property.name === key),
  );
  for (const key of unknownProperties) {
    grid.append(
      createElement(
        "div",
        {
          className: "field-row",
          attributes: { "data-search-alias": key },
        },
        createElement("div", {
          className: "field-name",
          text: displayLabel(key),
          title: key,
        }),
        createElement("div", { className: "field-value" }, jsonTree(value[key])),
      ),
    );
  }
  return grid;
}

function renderAuto(value, typeName, depth = 0) {
  if (depth > 8) {
    return jsonTree(value);
  }
  const schema = getType(typeName);
  if (!schema) {
    return value !== null && typeof value === "object" ? jsonTree(value) : primitiveNode(value);
  }
  switch (schema.kind) {
    case "bool":
    case "string":
    case "signed_integer":
    case "unsigned_integer":
    case "floating":
      return primitiveNode(value, schema.kind);
    case "enum":
      return value === null || value === undefined
        ? primitiveNode(value)
        : createElement("span", { className: "enum-value", text: value });
    case "optional": {
      if (value === null || value === undefined) {
        return createElement("span", { className: "null-value", text: "None" });
      }
      const payload =
        typeof value === "object" && !Array.isArray(value) && "$optional" in value
          ? value.$optional
          : value;
      return renderAuto(payload, schema.element_type, depth + 1);
    }
    case "blob_ref":
      return renderBlobReference(value);
    case "object":
      return renderObject(value, schema, depth);
    case "sequence":
    case "set":
      return renderCollection(value, schema, depth);
    case "map":
      return renderMap(value, schema, depth);
    default:
      return jsonTree(value);
  }
}

function viewTabs(capabilityState, onChange) {
  const tabs = createElement("div", { className: "view-tabs", attributes: { role: "tablist" } });
  for (const mode of ["auto", "tree", "raw"]) {
    const button = createElement("button", {
      className: `tab-button${capabilityState.viewMode === mode ? " active" : ""}`,
      type: "button",
      text: displayLabel(mode),
      attributes: {
        role: "tab",
        "aria-selected": String(capabilityState.viewMode === mode),
      },
    });
    button.addEventListener("click", () => {
      capabilityState.viewMode = mode;
      onChange();
    });
    tabs.append(button);
  }
  return tabs;
}

function copyButton(value) {
  const button = createElement("button", {
    className: "button ghost",
    type: "button",
    text: "Copy JSON",
  });
  button.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(safeStringify(value));
      showToast("JSON copied");
    } catch {
      showToast("Clipboard access failed", "error");
    }
  });
  return button;
}

function dataSummary(value, typeName) {
  const schema = getType(typeName);
  if (Array.isArray(value)) {
    return `${value.length} ${schema?.kind === "map" ? "entries" : "items"}`;
  }
  if (value !== null && typeof value === "object") {
    return `${Object.keys(value).length} fields`;
  }
  return displayLabel(schema?.kind ?? typeof value);
}

function applyDataSearch(rendered, query, emptyState) {
  const normalized = query.trim().toLocaleLowerCase();
  const selector = ".field-row, .data-table, .data-table tbody tr, .json-entry, .collection-item";
  const candidates = [...rendered.querySelectorAll(selector)];
  rendered.hidden = false;
  if (!normalized) {
    for (const candidate of candidates) {
      candidate.hidden = false;
    }
    emptyState.hidden = true;
    return;
  }

  const protectedCandidates = new Set();
  for (const candidate of candidates) {
    const directLabel = [...candidate.children].find(
      (child) => child.classList?.contains("field-name") || child.classList?.contains("json-key"),
    );
    const tableHeader = candidate.matches(".data-table")
      ? candidate.querySelector("thead")?.textContent
      : "";
    if (
      directLabel?.textContent.toLocaleLowerCase().includes(normalized) ||
      candidate.dataset.searchAlias?.toLocaleLowerCase().includes(normalized) ||
      tableHeader?.toLocaleLowerCase().includes(normalized)
    ) {
      protectedCandidates.add(candidate);
      for (const child of candidate.querySelectorAll(selector)) {
        protectedCandidates.add(child);
      }
    }
  }

  let matches = 0;
  for (const candidate of candidates) {
    const matchesQuery = candidate.textContent.toLocaleLowerCase().includes(normalized);
    candidate.hidden = !protectedCandidates.has(candidate) && !matchesQuery;
    if (!candidate.hidden) {
      matches += 1;
    }
  }
  if (!candidates.length) {
    matches = rendered.textContent.toLocaleLowerCase().includes(normalized) ? 1 : 0;
  }
  rendered.hidden = matches === 0;
  emptyState.hidden = matches !== 0;
}

function disclosureButton(label, rendered, open) {
  const button = createElement("button", {
    className: "button ghost data-tool-button",
    type: "button",
    text: label,
  });
  button.addEventListener("click", () => {
    for (const details of rendered.querySelectorAll("details")) {
      details.open = open;
    }
  });
  return button;
}

function renderDataPanel(container, value, typeName, capabilityState) {
  const redraw = () => renderDataPanel(container, value, typeName, capabilityState);
  let rendered;
  if (capabilityState.viewMode === "raw") {
    rendered = createElement("pre", { className: "raw-json", text: safeStringify(value) });
  } else if (capabilityState.viewMode === "tree") {
    rendered = jsonTree(value);
  } else {
    rendered = renderAuto(value, typeName);
  }
  const search = createElement("input", {
    className: "data-search",
    attributes: {
      "aria-label": "Search data",
      autocomplete: "off",
      placeholder: "Search data",
      type: "search",
    },
  });
  search.value = capabilityState.dataQuery;
  const emptyState = createElement("div", {
    className: "data-search-empty",
    text: "No matching fields or values",
  });
  emptyState.hidden = true;
  search.addEventListener("input", () => {
    capabilityState.dataQuery = search.value;
    applyDataSearch(rendered, capabilityState.dataQuery, emptyState);
  });

  const leftTools = createElement(
    "div",
    { className: "data-toolbar-group" },
    viewTabs(capabilityState, redraw),
  );
  if (capabilityState.viewMode !== "raw") {
    leftTools.append(search);
  }
  const rightTools = createElement(
    "div",
    { className: "data-toolbar-group" },
    createElement("span", { className: "data-summary", text: dataSummary(value, typeName) }),
  );
  if (rendered.querySelector("details")) {
    rightTools.append(
      disclosureButton("Expand all", rendered, true),
      disclosureButton("Collapse all", rendered, false),
    );
  }
  if (capabilityState.viewMode === "raw") {
    rightTools.append(copyButton(value));
  }
  const toolbar = createElement("div", { className: "toolbar data-toolbar" }, leftTools, rightTools);
  applyDataSearch(
    rendered,
    capabilityState.viewMode === "raw" ? "" : capabilityState.dataQuery,
    emptyState,
  );
  replaceChildren(container, toolbar, rendered, emptyState);
}

function pathLabel(path) {
  return path.length ? displayLabel(path.at(-1)) : "Request body";
}

function controlError(path, message) {
  throw new DevToolsError(`${path.length ? path.join(".") : "Request"}: ${message}`);
}

function scalarControl(typeName, schema, path) {
  const wrapper = createElement("div", { className: "form-field" });
  const label = createElement(
    "label",
    {},
    createElement("span", { text: pathLabel(path) }),
    createElement("code", { text: typeName }),
  );
  let input;
  if (schema.kind === "bool") {
    input = createElement("select");
    input.append(
      createElement("option", { value: "", text: "Select true or false" }),
      createElement("option", { value: "true", text: "True" }),
      createElement("option", { value: "false", text: "False" }),
    );
  } else if (schema.kind === "enum") {
    input = createElement("select");
    input.append(createElement("option", { value: "", text: "Select a value" }));
    for (const enumerator of schema.values ?? []) {
      input.append(
        createElement("option", {
          value: enumerator.name,
          text: enumerator.name,
        }),
      );
    }
  } else {
    input = createElement("input", {
      type: ["signed_integer", "unsigned_integer", "floating"].includes(schema.kind)
        ? "number"
        : "text",
      attributes: {
        placeholder: "Required",
        step: schema.kind === "floating" ? "any" : "1",
      },
    });
    if (schema.kind === "unsigned_integer") {
      input.min = "0";
    }
  }
  wrapper.append(label, input);

  const read = () => {
    if (input.value === "") {
      controlError(path, "a value is required");
    }
    if (schema.kind === "bool") {
      return input.value === "true";
    }
    if (schema.kind === "enum" || schema.kind === "string") {
      return input.value;
    }
    const numeric = Number(input.value);
    if (!Number.isFinite(numeric)) {
      controlError(path, "must be a finite number");
    }
    if (["signed_integer", "unsigned_integer"].includes(schema.kind)) {
      if (!Number.isSafeInteger(numeric)) {
        controlError(path, "must be a safe integer; use Raw JSON for larger values");
      }
      if (schema.kind === "unsigned_integer" && numeric < 0) {
        controlError(path, "must not be negative");
      }
    }
    return numeric;
  };
  return { element: wrapper, read };
}

function setControlDisabled(element, disabled) {
  for (const control of element.querySelectorAll("input, select, textarea, button")) {
    control.disabled = disabled;
  }
}

function optionalControl(typeName, schema, path) {
  const wrapper = createElement("fieldset", { className: "form-group" });
  const inner = buildFormControl(schema.element_type, path);
  const enabled = createElement("input", { type: "checkbox" });
  const toggle = createElement(
    "label",
    { className: "optional-toggle" },
    enabled,
    createElement("span", { text: `${pathLabel(path)} (optional)` }),
    createElement("code", { text: typeName }),
  );
  wrapper.append(toggle, inner.element);
  setControlDisabled(inner.element, true);
  enabled.addEventListener("change", () => setControlDisabled(inner.element, !enabled.checked));
  return {
    element: wrapper,
    read: () => (enabled.checked ? inner.read() : null),
  };
}

function objectControl(typeName, schema, path) {
  const fieldset = createElement("fieldset", { className: "form-group" });
  if (path.length) {
    fieldset.append(
      createElement("legend", {}, pathLabel(path), createElement("code", { text: typeName })),
    );
  }
  const controls = [];
  for (const property of schema.properties ?? []) {
    const control = buildFormControl(property.type, [...path, property.name]);
    controls.push([property.name, control]);
    fieldset.append(control.element);
  }
  if (!controls.length) {
    fieldset.append(createElement("span", { className: "muted", text: "Empty object" }));
  }
  return {
    element: fieldset,
    read: () => Object.fromEntries(controls.map(([name, control]) => [name, control.read()])),
  };
}

function fixedCollectionSize(schema) {
  if (!schema.fixed_size) {
    return null;
  }
  const size = (schema.arguments ?? []).find(
    (argument) => argument.kind === "unsigned_integer",
  )?.value;
  return Number.isSafeInteger(size) && size >= 0 ? size : null;
}

function collectionControl(typeName, schema, path) {
  const fieldset = createElement("fieldset", { className: "form-group" });
  fieldset.append(
    createElement("legend", {}, pathLabel(path), createElement("code", { text: typeName })),
  );
  const list = createElement("ol", { className: "form-list" });
  const controls = [];
  const fixedSize = fixedCollectionSize(schema);
  const elementType = collectionElementType(schema);
  const addItem = () => {
    const control = buildFormControl(elementType, [...path, String(controls.length)]);
    const item = createElement("li", { className: "form-list-item" }, control.element);
    if (fixedSize === null) {
      const remove = createElement("button", {
        className: "button ghost",
        type: "button",
        text: "Remove",
      });
      remove.addEventListener("click", () => {
        const index = controls.findIndex((entry) => entry.control === control);
        if (index >= 0) {
          controls.splice(index, 1);
        }
        item.remove();
      });
      item.append(remove);
    }
    controls.push({ control, item });
    list.append(item);
  };
  if (fixedSize !== null) {
    for (let index = 0; index < fixedSize; index += 1) {
      addItem();
    }
  }
  fieldset.append(list);
  if (fixedSize === null) {
    const add = createElement("button", {
      className: "button ghost",
      type: "button",
      text: "Add item",
    });
    add.addEventListener("click", addItem);
    fieldset.append(add);
  }
  return {
    element: fieldset,
    read: () => controls.map(({ control }) => control.read()),
  };
}

function mapControl(typeName, schema, path) {
  const fieldset = createElement("fieldset", { className: "form-group" });
  fieldset.append(
    createElement("legend", {}, pathLabel(path), createElement("code", { text: typeName })),
  );
  const list = createElement("ol", { className: "form-list" });
  const controls = [];
  const addEntry = () => {
    const index = controls.length;
    const key = buildFormControl(schema.key_type, [...path, String(index), "key"]);
    const value = buildFormControl(schema.mapped_type, [...path, String(index), "value"]);
    const fields = createElement("div", { className: "schema-form" }, key.element, value.element);
    const item = createElement("li", { className: "form-list-item" }, fields);
    const remove = createElement("button", {
      className: "button ghost",
      type: "button",
      text: "Remove",
    });
    remove.addEventListener("click", () => {
      const itemIndex = controls.findIndex((entry) => entry.key === key);
      if (itemIndex >= 0) {
        controls.splice(itemIndex, 1);
      }
      item.remove();
    });
    item.append(remove);
    controls.push({ key, value });
    list.append(item);
  };
  const add = createElement("button", {
    className: "button ghost",
    type: "button",
    text: "Add entry",
  });
  add.addEventListener("click", addEntry);
  fieldset.append(list, add);
  return {
    element: fieldset,
    read: () => controls.map((entry) => ({ key: entry.key.read(), value: entry.value.read() })),
  };
}

function rawJsonControl(typeName, path) {
  const wrapper = createElement("div", { className: "form-field" });
  const textarea = createElement("textarea", {
    className: "json-editor",
    attributes: { spellcheck: "false" },
  });
  textarea.value = "null";
  wrapper.append(
    createElement(
      "label",
      {},
      createElement("span", { text: pathLabel(path) }),
      createElement("code", { text: typeName || "untyped JSON" }),
    ),
    textarea,
    createElement("span", {
      className: "muted",
      text: "This type has no reliable form representation. Enter JSON directly.",
    }),
  );
  return {
    element: wrapper,
    read: () => {
      try {
        return JSON.parse(textarea.value);
      } catch (error) {
        controlError(path, `invalid JSON (${error.message})`);
      }
    },
  };
}

function buildFormControl(typeName, path = []) {
  const schema = getType(typeName);
  if (!schema) {
    return rawJsonControl(typeName, path);
  }
  if (isPrimitiveSchema(schema)) {
    return scalarControl(typeName, schema, path);
  }
  switch (schema.kind) {
    case "optional":
      return optionalControl(typeName, schema, path);
    case "object":
      return objectControl(typeName, schema, path);
    case "sequence":
    case "set":
      return collectionControl(typeName, schema, path);
    case "map":
      return mapControl(typeName, schema, path);
    default:
      return rawJsonControl(typeName, path);
  }
}

async function postRequest(capability, payload) {
  const endpoint = endpointByRel(capability, "invoke");
  if (!endpoint) {
    throw new DevToolsError("Manifest does not provide an invoke endpoint", {
      capability: capability.id,
    });
  }
  const params = {};
  if ((endpoint.params ?? []).includes("timeout_ms")) {
    params.timeout_ms = 5000;
  }
  const options = {
    method: "POST",
    cache: "no-store",
    redirect: "error",
  };
  if (payload !== undefined) {
    options.headers = { "Content-Type": "application/json" };
    options.body = JSON.stringify(payload);
  }
  const response = await fetch(sameOriginUrl(endpoint.path, params), options);
  if (!response.ok) {
    throw await readErrorResponse(response);
  }
  const text = await response.text();
  if (!text) {
    return null;
  }
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

function renderRequest(capability, body, header) {
  const capabilityState = getCapabilityState(capability.id);
  const section = createElement("section", { className: "section" });
  const formId = `capability-form-${capability.id.replace(/[^a-zA-Z0-9_-]+/g, "-")}`;
  const form = createElement("form", {
    className: "schema-form",
    attributes: { id: formId },
  });
  const requestControl = capability.request_type
    ? buildFormControl(capability.request_type)
    : {
        element: createElement("div", {
          className: "muted",
          text: "This capability does not accept a request body.",
        }),
        read: () => undefined,
      };
  const validation = createElement("div", { className: "validation-error", attributes: { role: "alert" } });
  const submit = createElement("button", {
    className: "button primary",
    type: "submit",
    text: "Run",
    attributes: { form: formId },
  });
  form.append(requestControl.element, validation);
  header.actions.append(submit);

  const response = createElement("div", { className: "response-panel" });
  const renderResponse = () => {
    header.status.textContent = capabilityState.loading
      ? "Sending…"
      : capabilityState.error
        ? "Request failed"
        : capabilityState.updatedAt
          ? `Last response ${timestampLabel(capabilityState.updatedAt)}`
          : "No request sent";
    if (capabilityState.loading) {
      replaceChildren(response, loadingInline("Waiting for response"));
    } else if (capabilityState.error) {
      replaceChildren(response, requestError(capabilityState.error));
    } else if (capabilityState.payload === undefined) {
      replaceChildren(
        response,
        createElement("span", {
          className: "muted",
          text: "This capability has not been invoked in this session.",
        }),
      );
    } else {
      renderDataPanel(
        response,
        capabilityState.payload,
        capability.response_type,
        capabilityState,
      );
    }
  };

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (capabilityState.loading) {
      return;
    }
    validation.textContent = "";
    let payload;
    try {
      payload = requestControl.read();
    } catch (error) {
      validation.textContent = error.message;
      return;
    }
    capabilityState.loading = true;
    capabilityState.error = null;
    submit.disabled = true;
    renderResponse();
    try {
      capabilityState.payload = await postRequest(capability, payload);
      capabilityState.responseType = capability.response_type ?? "";
      capabilityState.updatedAt = new Date();
      showToast("Capability completed");
    } catch (error) {
      capabilityState.error = normalizeError(error, capability);
    } finally {
      capabilityState.loading = false;
      submit.disabled = false;
      renderResponse();
    }
  });

  section.append(
    createElement("div", { className: "section-header" }, createElement("h2", { text: "Request" })),
    createElement("div", { className: "form-panel" }, form),
    createElement("div", { className: "section-header section" }, createElement("h2", { text: "Response" })),
    response,
  );
  body.append(section);
  renderResponse();
}

function parseBlobMetadata(response) {
  const raw = response.headers.get("X-DevTools-Metadata");
  if (!raw) {
    return null;
  }
  try {
    return JSON.parse(raw);
  } catch {
    return raw;
  }
}

function downloadName(capability, mime) {
  const extensionByMime = {
    "application/json": "json",
    "image/jpeg": "jpg",
    "image/png": "png",
    "text/plain": "txt",
  };
  const safeId = capability.id.replace(/[^a-zA-Z0-9._-]+/g, "-");
  return `${safeId}.${extensionByMime[mime] ?? "bin"}`;
}

async function createBlobPreview(capability, capabilityState, options = {}) {
  const { compact = false } = options;
  const { blob, mime, metadata, version } = capabilityState.blob;
  const blobUrl = ensureBlobUrl(capabilityState);
  const preview = createElement("div", {
    className: `blob-preview${compact ? " compact" : ""}`,
  });
  const metadataRow = createElement("div", { className: "blob-metadata" });
  metadataRow.append(
    chip(mime || "application/octet-stream"),
    chip(`${blob.size} bytes`),
    chip(`version ${version ?? "—"}`),
  );
  metadataRow.append(
    createElement("a", {
      className: "button ghost",
      text: "Save blob",
      href: blobUrl,
      attributes: { download: downloadName(capability, mime) },
    }),
  );
  preview.append(metadataRow);

  if (mime.startsWith("image/")) {
    preview.append(
      createElement("img", {
        attributes: {
          src: blobUrl,
          alt: `${capability.label || capability.id} preview`,
        },
      }),
    );
  } else if (
    (mime.startsWith("text/") || mime.includes("json")) &&
    blob.size <= maximumPreviewBytes
  ) {
    preview.append(
      createElement("pre", { className: "raw-json", text: await blob.text() }),
    );
  } else {
    preview.append(
      createElement("p", {
        className: "muted",
        text:
          blob.size > maximumPreviewBytes
            ? "Preview skipped because this blob is larger than 2 MiB."
            : "This MIME type has no inline preview.",
      }),
    );
  }
  if (!compact && metadata !== null) {
    preview.append(
      createElement("h3", { text: "Metadata" }),
      createElement("pre", { className: "raw-json", text: safeStringify(metadata) }),
    );
  }
  return preview;
}

async function requestBlobCapability(
  capability,
  forceFresh,
  onChange = () => {},
) {
  const capabilityState = getCapabilityState(capability.id);
  if (capabilityState.loading) {
    return;
  }
  const endpoint = endpointByRel(capability, "read");
  if (!endpoint) {
    capabilityState.error = new DevToolsError(
      "Manifest does not provide a blob GET endpoint",
      {
        capability: capability.id,
      },
    );
    await onChange();
    return;
  }

  capabilityState.loading = true;
  capabilityState.error = null;
  await onChange();
  try {
    const params = {};
    if ((endpoint.params ?? []).includes("timeout_ms")) {
      params.timeout_ms = 5000;
    }
    if (forceFresh && (endpoint.params ?? []).includes("fresh")) {
      params.fresh = true;
    }
    const response = await fetch(sameOriginUrl(endpoint.path, params), {
      cache: "no-store",
      redirect: "error",
    });
    if (!response.ok) {
      throw await readErrorResponse(response);
    }
    const blob = await response.blob();
    releaseBlobUrl(capabilityState);
    capabilityState.blob = {
      blob,
      mime: response.headers.get("Content-Type") || capability.mime || blob.type || "",
      metadata: parseBlobMetadata(response),
      version: response.headers.get("X-DevTools-Version"),
    };
    capabilityState.updatedAt = new Date();
  } catch (error) {
    capabilityState.error = normalizeError(error, capability);
  } finally {
    capabilityState.loading = false;
    await onChange();
  }
}

function blobReferenceError(message, value) {
  return createElement(
    "div",
    { className: "blob-reference-error" },
    createElement("span", { text: message }),
    value === undefined ? null : jsonTree(value),
  );
}

function renderBlobReference(value) {
  if (
    !value ||
    typeof value !== "object" ||
    Array.isArray(value) ||
    typeof value.capability !== "string"
  ) {
    return blobReferenceError("Blob reference has an invalid shape", value);
  }
  const capability = capabilityById(value.capability);
  if (!capability) {
    return blobReferenceError(
      `Blob capability '${value.capability}' is not in the manifest`,
    );
  }
  if (!isBlobCapability(capability)) {
    return blobReferenceError(`Capability '${value.capability}' is not a blob`);
  }

  state.inlineBlobIds.add(capability.id);
  const ownerId = state.selectedId;
  const capabilityState = getCapabilityState(capability.id);
  const image = (capability.mime ?? "").startsWith("image/");
  const action = createElement("button", {
    className: "button ghost blob-reference-action",
    type: "button",
    text: image ? "Load image" : "Load blob",
  });
  const content = createElement("div", { className: "blob-reference-content" });
  const card = createElement(
    "div",
    { className: "blob-reference" },
    createElement(
      "div",
      { className: "blob-reference-header" },
      createElement(
        "div",
        { className: "blob-reference-title" },
        createElement("strong", {
          text: capability.label || displayLabel(capability.id),
        }),
        createElement("code", { text: capability.id }),
      ),
      action,
    ),
    content,
  );

  const update = async () => {
    if (state.selectedId !== ownerId) {
      return;
    }
    action.disabled = capabilityState.loading;
    action.textContent = capabilityState.loading
      ? "Loading…"
      : capabilityState.error
        ? "Retry"
        : capabilityState.blob
          ? "Refresh"
          : image
            ? "Load image"
            : "Load blob";
    if (capabilityState.loading) {
      replaceChildren(content, loadingInline("Waiting for blob"));
    } else if (capabilityState.error) {
      replaceChildren(content, requestError(capabilityState.error));
    } else if (!capabilityState.blob) {
      replaceChildren(
        content,
        createElement("span", {
          className: "muted",
          text: "Preview is loaded only when requested.",
        }),
      );
    } else {
      replaceChildren(
        content,
        await createBlobPreview(capability, capabilityState, { compact: true }),
      );
    }
  };

  action.addEventListener("click", () => {
    void requestBlobCapability(capability, Boolean(capabilityState.blob), update);
  });
  void update();
  return card;
}

function renderBlob(capability, body, header) {
  const capabilityState = getCapabilityState(capability.id);
  const section = createElement("section", { className: "section" });
  const actionButton = createElement("button", {
    className: "button primary",
    type: "button",
    text: "Load blob",
  });
  header.actions.append(actionButton);
  const panel = createElement("div", { className: "blob-panel" });

  const update = async () => {
    if (state.selectedId !== capability.id) {
      return;
    }
    actionButton.disabled = capabilityState.loading;
    actionButton.textContent = capabilityState.loading
      ? "Loading…"
      : capabilityState.error
        ? "Retry"
        : capabilityState.blob
          ? "Refresh"
          : "Load blob";
    header.status.textContent = capabilityState.loading
      ? "Loading…"
      : capabilityState.error
        ? "Request failed"
        : capabilityState.updatedAt
          ? `Version ${capabilityState.blob?.version ?? "—"} · ${timestampLabel(capabilityState.updatedAt)}`
          : "Not loaded";
    if (capabilityState.loading) {
      replaceChildren(panel, loadingInline("Waiting for blob"));
      return;
    }
    if (capabilityState.error) {
      replaceChildren(panel, requestError(capabilityState.error));
      return;
    }
    if (!capabilityState.blob) {
      replaceChildren(
        panel,
        createElement("div", {
          className: "empty-state",
          text: "Blob content is requested only when you choose to load it.",
        }),
      );
      return;
    }

    replaceChildren(panel, await createBlobPreview(capability, capabilityState));
  };

  actionButton.addEventListener("click", () => {
    void requestBlobCapability(capability, Boolean(capabilityState.blob), update);
  });
  section.append(panel);
  body.append(section);
  void update();
}

function normalizeError(error, capability = null) {
  if (error instanceof DevToolsError) {
    if (!error.capability && capability) {
      error.capability = capability.id;
    }
    return error;
  }
  return new DevToolsError(error?.message || String(error), {
    capability: capability?.id,
  });
}

function renderUnsupported(capability, body) {
  body.append(
    createElement(
      "div",
      { className: "empty-state" },
      createElement("h2", { text: "Unsupported capability" }),
      createElement("p", {
        text: "This capability does not expose a supported endpoint.",
      }),
    ),
  );
}

function renderCapability(capability) {
  cleanupRenderedBlobs(capability.id);
  state.selectedId = capability.id;
  const body = createElement("div");
  const header = capabilityHeader(capability);
  body.append(header.element);
  if (endpointByRel(capability, "invoke")) {
    renderRequest(capability, body, header);
  } else if (isBlobCapability(capability)) {
    renderBlob(capability, body, header);
  } else {
    renderUnsupported(capability, body);
  }
  replaceChildren(elements.content, body);
  renderSidebar();
  elements.main.focus({ preventScroll: true });
}

function renderHome() {
  cleanupRenderedBlobs(null);
  state.selectedId = null;
  const capabilities = state.manifest?.capabilities ?? [];
  const body = createElement(
    "div",
    {},
    createElement(
      "header",
      { className: "page-header" },
      createElement(
        "div",
        {},
        createElement("span", { className: "eyebrow", text: "Service overview" }),
        createElement("h1", { text: state.discovery?.name || "FEI DevTools" }),
        createElement("p", {
          text: "Select a capability to inspect data or construct a request from its schema.",
        }),
      ),
    ),
  );
  const section = createElement("section", { className: "section" });
  section.append(
    createElement(
      "div",
      { className: "section-header" },
      createElement("h2", { text: "Current status" }),
      createElement("span", {
        className: "last-updated",
        text: `${capabilities.length} capabilities discovered`,
      }),
    ),
    createElement("div", { className: "data-panel" }, jsonTree(state.status)),
  );
  body.append(section);
  replaceChildren(elements.content, body);
  renderSidebar();
}

function renderRoute() {
  if (!state.manifest) {
    return;
  }
  const id = capabilityFromHash();
  if (!id) {
    renderHome();
    return;
  }
  const capability = state.manifest.capabilities.find((item) => item.id === id);
  if (!capability) {
    cleanupRenderedBlobs(null);
    state.selectedId = null;
    replaceChildren(
      elements.content,
      createElement(
        "div",
        { className: "error-state" },
        createElement("h1", { text: "Capability not found" }),
        createElement("p", { text: id }),
        createElement("a", { className: "button ghost", text: "Return home", href: "#" }),
      ),
    );
    renderSidebar();
    return;
  }
  renderCapability(capability);
}

async function refreshDevTools() {
  if (!state.discovery) {
    return;
  }
  elements.refresh.disabled = true;
  elements.refresh.textContent = "Refreshing…";
  try {
    const [capabilitiesResult, statusResult] = await Promise.allSettled([
      Promise.all([
        fetchJson(state.discovery.manifest),
        fetchJson(state.discovery.schemas),
      ]).then(([manifest, schemas]) => ({
        manifest: validateManifest(manifest),
        schemas: validateSchemas(schemas),
      })),
      fetchJson(state.discovery.status).then((status) => {
        if (!status || typeof status !== "object") {
          throw new DevToolsError("Status response must be an object");
        }
        return status;
      }),
    ]);

    const errors = [];
    if (capabilitiesResult.status === "fulfilled") {
      state.manifest = capabilitiesResult.value.manifest;
      state.schemas = capabilitiesResult.value.schemas;
    } else {
      errors.push(`Capabilities: ${normalizeError(capabilitiesResult.reason).message}`);
    }
    if (statusResult.status === "fulfilled") {
      state.status = statusResult.value;
      updateStatusSummary();
    } else {
      errors.push(`Status: ${normalizeError(statusResult.reason).message}`);
    }

    if (capabilitiesResult.status === "fulfilled") {
      const selectedStillExists = state.manifest.capabilities.some(
        (capability) => capability.id === state.selectedId,
      );
      if (state.selectedId && !selectedStillExists) {
        window.history.replaceState(null, "", `${window.location.pathname}${window.location.search}`);
        renderHome();
      } else {
        renderRoute();
      }
    } else if (statusResult.status === "fulfilled" && !state.selectedId) {
      renderHome();
    }

    if (errors.length) {
      setConnection("error", errors.length === 2 ? "Disconnected" : "Refresh incomplete");
      showToast(errors.join(" · "), "error");
    } else {
      setConnection("connected", "Connected");
      showToast("DevTools refreshed");
    }
  } catch (error) {
    setConnection("error", "Refresh failed");
    showToast(normalizeError(error).message, "error");
  } finally {
    elements.refresh.disabled = false;
    elements.refresh.textContent = "Refresh";
  }
}

function renderBootError(error) {
  replaceChildren(
    elements.content,
    createElement(
      "div",
      { className: "error-state" },
      createElement("span", { className: "eyebrow", text: "Connection failed" }),
      createElement("h1", { text: "DevTools is unavailable" }),
      createElement("p", { text: error.message }),
      (() => {
        const retry = createElement("button", {
          className: "button primary",
          type: "button",
          text: "Retry connection",
        });
        retry.addEventListener("click", boot);
        return retry;
      })(),
    ),
  );
}

async function boot() {
  setConnection("pending", "Connecting");
  elements.refresh.disabled = true;
  replaceChildren(elements.capabilityNav);
  try {
    const discovery = validateDiscovery(await fetchJson("/"));
    const [manifest, schemas, status] = await Promise.all([
      fetchJson(discovery.manifest),
      fetchJson(discovery.schemas),
      fetchJson(discovery.status),
    ]);
    state.discovery = discovery;
    state.manifest = validateManifest(manifest);
    state.schemas = validateSchemas(schemas);
    if (!status || typeof status !== "object") {
      throw new DevToolsError("Status response must be an object");
    }
    state.status = status;
    elements.serviceName.textContent = discovery.name;
    elements.protocolVersion.textContent = `Protocol v${discovery.version}`;
    setConnection("connected", "Connected");
    updateStatusSummary();
    renderSidebar();
    renderRoute();
  } catch (error) {
    const normalized = normalizeError(error);
    setConnection("error", "Disconnected");
    renderBootError(normalized);
  } finally {
    elements.refresh.disabled = !state.discovery;
  }
}

elements.search.addEventListener("input", renderSidebar);
elements.sidebarBlobToggle.addEventListener("click", () => {
  setShowSidebarBlobs(!state.showSidebarBlobs);
});
elements.refresh.addEventListener("click", refreshDevTools);
elements.sidebarToggle.addEventListener("click", () => {
  setSidebarOpen(!elements.sidebar.classList.contains("open"));
});
window.addEventListener("hashchange", renderRoute);
window.addEventListener("beforeunload", () => {
  for (const capabilityState of state.capabilityState.values()) {
    releaseBlobUrl(capabilityState);
  }
});

void boot();
