# Luau 2D playtest projects

This directory contains four independent project configurations that share the
same script and texture asset directory. `project.yaml` is the original collect
game. The other three projects exercise different playtest action contracts:

| Project file | Interface | Contract under test |
| --- | --- | --- |
| `platformer.project.yaml` | `platformer.main` | Simultaneous analog movement and jump input over multiple ticks |
| `pointer_puzzle.project.yaml` | `pointer-puzzle.main` | Stateful click selection followed by world-coordinate drag actions |
| `card_battle.project.yaml` | `card-battle.main` | Semantic turn actions selected from structured game state |

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

The playtest programs are intentionally not stored as fixed command sequences.
Agents are expected to inspect each interface and construct different Luau
control logic with `play-run`.
