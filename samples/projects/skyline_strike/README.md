# Skyline Strike

`Skyline Strike` is a 16:9 horizontal shoot-em-up implemented entirely in
Luau gameplay scripts. Open this directory in the Entisium Editor and press
Play.

Project scripts are kept directly under `assets/`, with images in
`assets/images/`:

- `game.luau` is the single project entry and composes the game Plugins.
- `gameplay.luau` contains game state and gameplay systems.
- `hud.luau` contains presentation systems.
- `playtest.luau` contains the agent action and observation contract.
- `images/` contains all runtime textures.

Controls:

- Move with WASD or the arrow keys.
- Fire with Space or Z.

The project has one entry:

```yaml
plugin: project://game.luau#SkylineStrikePlugin
```

Normal Editor Play starts it in interactive mode. Agent testing starts the same
entry with `runtime_play({mode: "playtest"})`, exposing the
`skyline-strike.main` interface and pausing the fixed clock between
agent-controlled steps.

The first playable slice contains a scrolling star layer, deterministic enemy
waves, player and enemy projectiles, circular hit detection, a dreadnought
boss, screen shake, explosions, HUD, victory/defeat states, and structured
Playtest observations.
