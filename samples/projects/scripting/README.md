# Luau 2D playtest projects

This directory contains six independent project configurations that share the
same script and texture asset directory. `project.yaml` is the original collect
game. Three of the other projects exercise game-specific playtest contracts:

| Project file | Interface | Contract under test |
| --- | --- | --- |
| `platformer.project.yaml` | `platformer.main` | Simultaneous analog movement and jump input over multiple ticks |
| `pointer_puzzle.project.yaml` | `pointer-puzzle.main` | Stateful click selection followed by world-coordinate drag actions |
| `card_battle.project.yaml` | `card-battle.main` | Semantic turn actions selected from structured game state |
| `checkpoint_render.project.yaml` | `checkpoint-arena.main` | Keyboard/Agent actions, live status HUD, checkpoint restore, and alternate branching |

`ui.project.yaml` is an interactive Runtime Host integration sample for Luau,
UI layout and rendering, reflected text components, standard button behavior,
the Runtime Host's embedded fallback font, and real mouse input. Hover or click
the face to exercise pointer interaction.

When running under `fei-agentd`, the built-in `runtime.pointer` playtest
interface can position the pointer and hold `Left`, `Right`, or `Middle` for a
bounded step. This makes UI clicks reproducible without desktop automation.

Build the supervisor, CLI, and Runtime Host before launching a project:

```powershell
xmake build -y fei-agentd
xmake build -y fei-ctl
xmake build -y fei-runtime-host
```

For example, launch the platformer from the repository root:

```powershell
build/windows/x64/debug/fei-agentd.exe `
  --project samples/projects/scripting/platformer.project.yaml `
  --runtime build/windows/x64/debug/fei-runtime-host.exe `
  --port 8091
```

Then discover its contract or execute an ad-hoc control program:

```powershell
build/windows/x64/debug/fei-ctl.exe --port 8091 play-interfaces
build/windows/x64/debug/fei-ctl.exe --port 8091 play-run --eval `
  'return play.step("platformer.main", {horizontal=1, jump=true}, 72)'
```

Expected completion conditions are:

- Platformer: one combined right-and-jump action crosses the barrier and
  changes `won` to `true`.
- Pointer puzzle: click and drag all three pieces to the target with the same
  id, producing `placed = 3` and `complete = true`.
- Card battle: choose available damage cards from `hand` until `enemy_hp = 0`
  and `status = "won"`.
- Rendered checkpoint arena: run `checkpoint_render.retry.luau` through
  `fei-ctl play-run --stdin`. It captures the initial frame, despawns the enemy,
  restores and immediately recaptures the checkpoint, then renders a jumping
  branch. The initial and restored PNG files should be byte-identical. Run
  Runtime Host directly for continuous human play with `A`/`D` (or arrow keys)
  to move, `W`/up arrow to jump, and `Space` to attack; Agent actions use the
  same gameplay controller. `PageUp` stores a strict quick-save and `PageDown`
  restores it without advancing the restored simulation state.

Launch that continuous keyboard mode from the repository root with:

```powershell
build/windows/x64/debug/fei-runtime-host.exe `
  samples/projects/scripting/checkpoint_render.project.yaml
```

The playtest programs are intentionally not stored as fixed command sequences.
Agents are expected to inspect each interface and construct different Luau
control logic with `play-run`.
