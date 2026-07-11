---
name: devtools
description: Use the fei DevTools HTTP API to inspect or control a running sample. Use when diagnosing rendering output, capturing frames, inspecting RenderGraph or graphics cache state, checking DevTools status, or sending input commands to a sample target.
---

# DevTools

Use the agent-facing HTTP API exposed by `devtools::CorePlugin` and its provider
plugins. Discover the active protocol at runtime instead of assuming every
sample exposes the same capabilities.

## Workflow

1. Identify the target process and its DevTools base URL. The default is
   `http://127.0.0.1:8080`; inspect the sample's `devtools::Config` when it
   differs.
2. If the target is not running and the task authorizes running it, build and
   start it. Track processes started for the task and stop only those processes
   during cleanup.
3. Request `/` for service discovery, then request the returned manifest and
   schemas endpoints.
4. Select capabilities from the manifest by `id` and follow their declared
   endpoints. Do not guess endpoints that the manifest does not advertise.
5. Capture the minimum evidence needed, perform any authorized command, then
   capture fresh evidence for comparison.

## Read Capabilities

- Read `/api/v1/status` to confirm readiness, pending requests, cached data, and
  active subscriptions.
- For a snapshot, call the capability's `get` endpoint. Use `fresh=true` only
  when current state is required; snapshots are generated on demand, so do not
  continuously poll fresh snapshots.
- Snapshot responses contain `id`, `schema`, `version`, and `data`. Use the
  capability's `data_type` with the schemas endpoint when field meaning is
  unclear.
- For a frame, prefer the blob capability's one-shot `get` endpoint with
  `fresh=true`. Save the binary response to a temporary image and inspect it.
  Use the stream endpoint only when continuous monitoring is necessary.

## Send Commands

1. Read the command capability's `request_type` and `response_type`.
2. Resolve `request_type` through the schemas endpoint and construct the exact
   JSON object. Object fields are strict, and enum inputs use reflected names
   rather than numeric values.
3. POST JSON to the manifest's command endpoint with
   `Content-Type: application/json`.
4. Treat commands as state-changing. After simulated key presses, send matching
   releases or call `input.clear` before cleanup.
5. Call `devtools.shutdown` only when the task explicitly requires stopping the
   target and the process is in scope; the command also requires agent mode.

## Diagnose Failures

- Connection refused: confirm the process, configured host and port, and that
  `devtools::CorePlugin` is installed.
- Missing capability: inspect the sample's provider plugins; the core plugin
  does not supply rendering or input capabilities.
- Timeout: confirm the engine is ticking and the timeout permits the relevant
  schedule to run.
- HTTP error: preserve the structured error and report its status, capability,
  and message.

## Report Results

Report the capability and version used, summarize the observed state, and keep
captured artifacts only when useful to the task or explicitly requested.
