# Agent evals

These evals run the production EntisiumAgent prompt, model adapter and tool wrappers.
They are separate from deterministic unit tests and do not run as part of `npm test`.


## Framework examples and task authoring

The current focus is the harness contract, not benchmark coverage. Three existing tasks
demonstrate single-turn repair, multi-turn editing, and runtime interaction:

```sh
npm run agent:eval -- --list --suite framework
npm run agent:eval -- --validate --suite framework
```

Validation uses no model but requires a built native Runtime. Other existing tasks
remain available through their previous CLI selections.

```text
agent/evals/
  main.ts                   # CLI entry point
  suites.ts                 # Task selections
  tsconfig.json             # Standalone typecheck; no emitted files
  harness/                  # Discovery, runner, environments, evidence, regrading
  shared/                   # Reusable gameplay fixture and verification helpers
  fixtures/                 # Shared project assets
  tasks/repair-air-jump/
    task.json               # Version, tags, environment, ordered instruction files
    01-repair.md            # Exact user instruction
    project/                # Initial project copied into each isolated trial
    reference/repair.luau   # Reference output, outside the Agent workspace
    index.ts                # Connect data to implementation with defineTask()
    reference.ts            # Execute the reference via the same tools
    verify.ts               # Independent outcome verification and evidence grading
```

Task-specific code and assets live together. Only reusable implementation goes into
`harness/` or `shared/`. Eval code imports production Agent and DevKit package exports;
it is not part of the Agent product build. The CLI runs it directly through `tsx`.
Task discovery loads each trusted local `index.ts`; there is no configurable entrypoint,
expression language, or manual registration table.

From the repository root, build shared package declarations and typecheck evals with:

```sh
npm run build:shared
npm run typecheck:evals --workspace @entisium/agent
```

The root `npm run typecheck` also includes this check. Agent tests still live under
`agent/tests` and import the eval modules directly.

`task.json` is validated by `harness/task-definition.ts`. For example:

```json
{
  "id": "repair-air-jump",
  "version": 2,
  "tags": ["scripting", "debugging"],
  "environment": {
    "runtime": "native",
    "fixture": "project",
    "writable": true,
    "interface": "capability.main"
  },
  "steps": [{ "id": "repair", "instruction": "01-repair.md" }]
}
```

New instructions should contain the objective and necessary constraints, not verifier
internals. The four newly migrated single-turn edit prompts retain their historical
wording, including replay details, to preserve the tasks during this structural change.
Each additional step is another user message in the same conversation and project.
The reference receives its step ID; independent verification receives named snapshot
paths through `context.steps`, plus the final `context.project`, output directory and
cancellation signal. Outcome checks are exposed as typed `evidence.verification`;
runtime observations stay in `evidence.state`. A missing checkpoint must fail its check.

`suites.ts` contains task ID lists only. A task may belong to several suites; `framework`
and `capability` deliberately overlap. Runtime selection uses `environment.runtime`,
never suite membership. Existing suite names are compatibility selections, not a task
type taxonomy or a claim of benchmark maturity. Models, trial counts and execution
budgets are run options rather than part of the task definition.

To add a task, create `tasks/<id>/task.json`, its instruction and project files, and an
`index.ts` with a named `task` export:

```ts
import { defineTask, taskDirectory } from "../../harness/task-definition.js";
import { grade } from "./verify.js";
import { reference } from "./reference.js";

export const task = defineTask(taskDirectory("my-task"), { grade, reference });
```

`harness/discovery.ts` scans immediate subdirectories with `task.json`, validates
metadata, then loads `index.ts` in directory-name order. Directory names must match task
IDs. Duplicate IDs, missing entrypoints, invalid exports and manifest/export mismatches
fail discovery. Directories without `task.json` are ignored. These modules are trusted
repository code, not code supplied by the evaluated Agent.

No registry or runner edit is required. New tasks appear in `--list`, `--suite all`, and
`--case <id>`. Add their IDs to `suites.ts` only when they should join a named selection;
the default selection remains project and behavior. All 20 tasks now use the same
layout. Include a passing reference and a plausible failing outcome control. The
`framework` suite continues to select three representative examples.

Simulated failure scenarios declare `environment.fault` as `timeout`, `start`, or `write`
in their task manifest. The environment adapter applies these faults without task-ID
special cases. Task-specific grading and references remain ordinary TypeScript files.

Increment the task version when instructions, fixture semantics or required behavior
change. The two migrated editing tasks are version 2 because their instructions no
longer explain internal replay probes. New manifests retain version, environment, tags
and named steps. Regrading rejects recorded version/environment changes and still
checks exact prompts. Older evidence with `state.verification` is read compatibly.
Versioning is explicit: authors must bump it when changing fixture semantics. The five
newly migrated tasks retain version 1, original instructions and initial project bytes.
All migrated environment manifests now explicitly identify the fixture directory: older runs
with generated-fixture environment metadata are rejected by the existing environment
compatibility check and require fresh trials.

From the repository root:

```sh
npm run agent:eval -- --list
npm run agent:eval -- --validate
npm run agent:eval -- --validate --suite native
npm run agent:eval -- --case double-speed --trials 3
npm run agent:eval -- --suite all --trials 3
npm run agent:eval -- --validate --suite capability
npm run agent:eval -- --suite capability --trials 3 --timeout-ms 180000 --max-tool-calls 60
npm run agent:eval -- --regrade .entisium/evals/<original-run-id>
npm run agent:eval -- --regrade .entisium/evals/<original-run-id> --verify-saved
```

Agent mode uses the saved Editor credentials/model, or `--provider ID --model ID`.
It calls the actual model and incurs its normal usage. Default runs include six project
tasks and four behavior tasks. The two native smoke cases and eight capability cases
are opt-in (`--suite native`, `--suite capability`, `--suite all`, or their case IDs),
require `entisium-runtime-host`, and use isolated
hidden processes. Set `ETS_RUNTIME_HOST_PATH` for a non-default executable.

`--validate` executes trusted reference solutions through the same tools and graders,
without a model. It proves that the fixtures and positive grading paths work; it is
never a capability score. Unit tests also reject broken outcomes and exercise failure
and budget reporting. The original twelve tasks form the initial regression candidate
set; the eight capability tasks exercise longer workflows. Neither set is a calibrated
release gate. Native rotation tests are smoke tests, not a gameplay/win-rate benchmark.

## Capability suite

| Case | Objective | Independent outcome checks |
| --- | --- | --- |
| repair-luau-startup | Repair a syntax error | Startup state and original physics/scoring |
| repair-interaction-crash | Repair an interaction-triggered exception | Repeated interactions score +1 each |
| verify-double-movement | Change speed from 4 to 8 | Positive/negative displacement; preserved jump/gravity |
| repair-air-jump | Prevent repeated jumps in air | Ground jump succeeds, airborne inputs do not relaunch |
| repair-premature-victory | Repair goal threshold | No early victory; crossing the actual goal wins |
| preserve-change-across-turns | Change speed, then jump speed | First-turn snapshot is 8/5; final result is 8/6 |
| fixed-course-win | Jump over a wall and finish | Actual victory, zero collisions, <=120 ticks |
| reactive-gate-crossing | Observe a changing gate and cross | Actual victory, zero collisions, <=180 ticks |

The fixture is a small deterministic state-based microgame executed by the real native
engine (not a rendered game benchmark). All eight native gameplay tasks store explicit initial projects in their task directories.
The shared trusted replay wrapper lives in `capability-fixture.ts`; the canonical physics module is pinned in
`agent/evals/fixtures/capability/gameplay.luau`. It does not depend on changing samples.
The six editing tasks permit only the gameplay module to be edited. Play-only tasks
have no write or unrestricted inspection tool and cannot reset the game after play starts.

After each editing trial, the runner stops the Agent-owned process, creates fresh
verification projects outside the Agent's tool-visible root, and copies only the candidate
gameplay module into a trusted wrapper. It replays fixed actions from two initial states
(grounded and airborne, different positions/goals). A TypeScript physics oracle compares
initialization and eight checkpoints per scenario with 0.002 floating-point tolerance.
Verification does not trust the Agent's changed wrapper or its claimed observations.
Both the oracle and native reference controls are tested; this bounded probe set does
not prove all possible gameplay behavior.

The multi-turn task uses the same EntisiumAgent instance and conversation history. Each
turn's project is snapshotted before the next user message. The first and final snapshots
are independently replayed, so replacing the first turn's work cannot receive full credit.
All prompts, including follow-ups, are saved in the trial manifest; calls include turn indices.

Reports separate `Outcome` (independent behavior), `Agent verification` (gameplay after
restarting with the final saved edits, in each turn) and complete-task pass. Detailed checks
also record constraints and cleanup. A passing verification-action check only shows that
the Agent exercised the saved project; it does not prove its verbal interpretation is sound.
An Agent that writes correct code but does not verify it can pass Outcome and fail overall.
`verification/verification.json` contains expected/actual states and the replay transcript;
multi-turn trials also retain the first-turn verification beneath that directory.

Native negative controls are deliberately opt-in, keeping normal unit tests GPU-independent:

```powershell
$env:ETS_EVAL_NATIVE_TESTS = '1'
npm run test --workspace @entisium/agent -- tests/capability-native.test.ts
Remove-Item Env:ETS_EVAL_NATIVE_TESTS
```

They reject all five uncorrected single-turn edit fixtures, a lost first-turn change,
and a forged candidate harness. An additional control verifies saved code from a timed-out
run while preserving the timeout status. `--validate --suite capability` runs the eight positive
reference solutions. Run both when changing native fixture behavior or the oracle.

## Implementation

- `harness/discovery.ts`: automatic task discovery and module/manifest validation.
- `harness/task-definition.ts`, `tasks/`: validated task data adapter and all 20 colocated implementations.
- `suites.ts`: overlapping task selections, independent of environment configuration.
- `harness/environment.ts`: real project tools; stateful simulated runtime for non-native cases;
  production RuntimeSession for native cases.
- `harness/runner.ts`: isolated trials, budgets, evidence, grading and summary aggregation.
- `main.ts`: CLI, model selection and JSON/Markdown reports.
- `shared/capability-fixture.ts`, `shared/capability-verifier.ts`: trusted native replay fixtures and independent verification.

Every trial gets a new project directory. The model only receives project-scoped tools;
reference solutions, graders, reports, and other trials are outside its tool-visible root.
Native play tasks disable writes and unrestricted runtime inspection. The pinned native
fixture is derived from `samples/browser_project/project`, but runs natively. Its image
and script are included here so sample changes cannot silently change the benchmark.

Native state is observed independently before stopping the process. Graders distinguish
an Agent's explicit stop call from runner cleanup. Configuration tasks compare parsed
values and preserve unrelated fields/files; JSON formatting is not prescribed. Behavioral
reporting checks use task-specified final markers, not unrestricted semantic judging.
Read the complete answer during review: markers alone do not establish nuanced honesty.

## Reports and reproducibility

Runs are retained in a unique directory under `.entisium/evals` (already gitignored).
`--output` changes the parent directory. Projects are retained for inspection, never
reused for another trial. Delete individual run directories manually when no longer needed.

Each trial saves its prompt, system prompt, tool schemas, model identifier, fixture hash
and budgets in `manifest.json`; model events in `trace.jsonl`; calls, answer and runtime
state in `evidence.json`; and checks/status in `result.json`. The run contains `report.json`
and `report.md`. Traces may contain project content and model text; treat them as local
development artifacts. Credentials are not intentionally included. Provider-reported token
usage is recorded when available; missing usage is null, not zero. Prices are not inferred.
Provider stop reasons are retained in results. `stopReason=length` is a model-output
budget failure, even when the underlying Agent loop returns normally with no final text.
Regrading older traces backfills this diagnosis. New manifests also record the configured
model output limit and context window; these are not changed by the evaluator.

Defaults: three Agent trials, one reference trial, 120 seconds and 40 tool calls per trial.
For the capability baseline, use the explicit 180-second / 60-call command above.
Trusted verification has a separate 120-second cooperative deadline after Agent cleanup;
reported elapsed time includes this work. Model token usage excludes trusted verification.
It also assesses retained candidate code after an Agent budget/model failure, while the
overall status remains failed. Explicit user cancellation skips new verification work.
Runs are sequential to avoid GPU/process interference. The time limit is cooperative:
model/tool implementations must honor cancellation; cleanup is awaited and can extend
wall-clock duration. Ctrl+C cancels the current trial and stops dispatching new ones.

Statuses separate failed assertions, budget exhaustion, model errors, environment errors,
and grader errors. Failures of tools requested by the Agent are retained in the trace;
the Agent can recover from them. Preflight/reference validation is needed to distinguish
broken runtime infrastructure from an Agent that mishandles a runtime error.

Compare the same task/fixture/tool/prompt versions and budgets across models. Review
per-case results and failed traces, not only aggregate passes. Three trials are a small
sample; the all-passed column is an empirical stability signal, not an estimate of pass^k
with statistical confidence. Add real failures as new tasks, and keep harder capability
tasks separate from cases that later become stable regression gates.

After fixing a grader, `--regrade` reads an original run's retained evidence/projects and
writes a separate report with provenance links. It does not contact the model, replay
actions, or overwrite the original. Original timing/usage metrics remain unchanged.
By default only completed passed/failed trials are re-scored; infrastructure and budget
failures stay failures. Changed task or follow-up prompts require a fresh trial. Ordinary
regrading reuses recorded verification checks. Add `--verify-saved` to replay capability
projects with the current oracle/probes and retain fresh verification artifacts. This also
adds outcome checks to old budget/model failures without changing their overall status.
No model calls are made, and original timing/usage metrics remain baseline metrics rather
than including the new verification work. Use the original run as
input, not a previous regrade report; retain its files unmodified. Regrading confirms
the revised verdict on old evidence, not fresh model reliability.

To add a case, specify its observable success criteria in the user prompt, add a known
working reference and a grader that rejects a plausible wrong result, and validate it.
Do not grade against a hidden required file name or arbitrary tool order.
