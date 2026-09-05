---
name: playtest
description: Play and test Entisium project-based games, verify gameplay mechanics, attempt requested objectives, and reproduce gameplay bugs using structured Playtest interfaces. Default to the hidden native Runtime MCP; use a browser only for explicit browser requests or browser-specific behavior.
---

# Playtest

## Select the runtime

Default to **entisium-runtime**, which launches the native Runtime Host with a hidden window. An open Editor page is not a reason to choose the browser. Use the browser only when the user requests it or the target depends on WebAssembly, browser WebGPU, browser input, or webpage integration. Native results do not establish browser compatibility.

Identify tools by their server as well as their name: both MCP servers expose `runtime_play`, but their parameters and waiting behavior differ. Inspect the available tool schemas rather than inventing a tool namespace.

| Entry point | Start | Step / segment completion | Capture |
| --- | --- | --- | --- |
| Native Runtime MCP (default) | `runtime_play({project: absoluteProjectYaml})` | Returned directly; no polling. Segment uses `max_ticks` (currently 1–600). | `runtime_capture` |
| Browser built-in Pi tools | `runtime_play({mode: "playtest"})` for the current Editor project | Tools wait internally. Segment uses `maxTicks` (currently 1–3600). | `runtime_observe` |
| External Editor MCP | Same browser startup | Poll the matching `play_step_status` / `play_segment_status` with `requestId`; terminal reads consume the result. Segment uses `maxTicks`. | `runtime_observe` |

Prefer current advertised schemas if these interfaces change. Do not apply the native MCP's synchronous semantics to the external Editor MCP.

## Prepare

1. Resolve the intended project file and test objective from the task. Separate a brief exploratory play session from a requested win, reproduction, or regression check. For an unspecified exploration, choose a bounded sample of gameplay instead of attempting unlimited retries or a full playthrough.
2. Check `runtime_status`. Reuse a healthy native session only when its project and state fit the test. Do not replace another project's active session just to run this test; use an isolated connection or resolve that conflict first.
3. If the native host is missing or stale after engine changes, build from the repository root with `xmake build -y entisium-runtime-host`. Script and asset edits normally require restarting the game, not rebuilding C++.
4. Start the project with an absolute path. Native `runtime_play` waits for a completed readiness inspection. For the browser, confirm runtime readiness before issuing gameplay commands.
5. Call `play_interfaces`, read the chosen interface's action and observation schemas, then call `play_observe`. Do not assume field names or victory conditions from another game, including Skyline Strike.

If the native MCP is unavailable, inspect the repository's `docs/runtime-mcp.md` and its `editor/host/runtime-mcp-main.ts` entry point. Use the configured stdio service or an MCP client connected to that entry point when available. A plain CLI launch with `--hidden` does not itself attach the control channel or provide supervised Playtest. Report an unresolved native setup problem rather than silently switching to a browser.

## Play and evaluate

- Use structured observations for decisions and captures for visual evidence. Read [observations.md](references/observations.md) when interpreting spatial state, incomplete observations, or choosing evidence.
- Use `play_step` for a single action. Omit `ticks` unless an override is useful and the interface permits it. Verify actual state changes, not just success envelopes or increasing frame counters.
- Use short `play_segment` controllers when actions need per-tick feedback. Read [segments.md](references/segments.md) before writing one. Bound each segment and return to the model when the objective, failure condition, or a meaningful change calls for a new decision.
- Run one gameplay operation at a time. For auto-wait tools, use their final result without extra status calls. For external Editor MCP, only one caller should consume the terminal status; wait briefly between pending polls.
- Stop acting on the task's success or failure condition. Retry or restart only when the requested task calls for it; do not automatically start a new round.
- Playing a game does not authorize editing its scripts, changing health or difficulty, skipping progression, or using state mutation to manufacture a win. Use checkpoints or state setup when the test calls for them, and report their use. Fix code when fixing is part of the user's request.

## Interruptions and cleanup

Do not automatically resubmit a timed-out action: it may have executed. Preserve its request ID and inspect logs/status before choosing recovery. The native MCP marks uncertain execution failed and requires stop/start before continuing; treat a restarted game as a new run. Browser Pi tools cancel a segment internally; cancelling a step currently stops the runtime. External Editor MCP exposes `play_segment_cancel`, whose result already consumes the terminal status. Never poll again after consuming a terminal result.

Stop sessions started for the task when finished unless the user wants them left running. Leave pre-existing sessions available, with no continuing action or held input. Do not stop unrelated processes. Capture useful evidence before stopping; `runtime_logs` retains the native log tail after stop.

Report the project and runtime used, what was attempted, the observed outcome, and any material untested behavior. Distinguish tool success, a passing gameplay assertion, and actual objective completion. Do not claim improved win rate or browser correctness from a short native smoke test.

This repository skill guides clients that load it. Adding this file alone does not install a skill loader in the browser's built-in Pi agent.
