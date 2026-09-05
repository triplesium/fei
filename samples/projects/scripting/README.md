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

The declarations are examples for projects loaded through the Web Editor. Copy
or rename the desired configuration to `project.yaml`, open its directory in
the Editor, and use the structured MCP workflow described in
[`docs/playtest.md`](../../../docs/playtest.md). The Editor also exposes
normalized pointer input as a fallback for UI projects.

Expected completion conditions are:

- Platformer: one combined right-and-jump action crosses the barrier and
  changes `won` to `true`.
- Pointer puzzle: click and drag all three pieces to the target with the same
  id, producing `placed = 3` and `complete = true`.
- Card battle: choose available damage cards from `hand` until `enemy_hp = 0`
  and `status = "won"`.
- Rendered checkpoint arena: use `A`/`D` (or arrow keys) to move, `W`/up arrow
  to jump, and `Space` to attack. Agent actions use the same gameplay
  controller. `PageUp` stores a strict quick-save and `PageDown` restores it
  without advancing the restored simulation state.

Launch that continuous keyboard mode from the repository root without relying
on a platform-specific build-output path:

```powershell
xmake run entisium-runtime-host `
  samples/projects/scripting/checkpoint_render.project.yaml
```

The playtest programs are intentionally not stored as fixed command sequences.
Agents are expected to inspect each interface and choose actions from the live
observation.

For automated runs, add `--hidden` to create the native window hidden from the
start, while retaining rendering and runtime inspection:

```powershell
xmake run -y entisium-runtime-host --hidden samples/projects/skyline_strike/project.yaml
```

This mode still requires a desktop graphics environment. It does not receive
normal keyboard or mouse interaction; use runtime control interfaces instead.
The default launch continues to show the game window.
