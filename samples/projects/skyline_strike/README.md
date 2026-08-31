# Skyline Strike

`Skyline Strike` is a 16:9 horizontal shoot-em-up implemented entirely in
Luau gameplay scripts. Open this directory in the Entisium Editor and press
Play.

Project scripts are kept directly under `assets/`, with images in
`assets/images/`:

- `game.luau` is the normal game entry.
- `gameplay.luau` contains game state and gameplay systems.
- `hud.luau` contains presentation systems.
- `playtest.luau` contains the agent-testing entry and interface.
- `images/` contains all runtime textures.

Controls:

- Move with WASD or the arrow keys.
- Fire with Space or Z.

The default project entry runs continuously for normal play. For deterministic
agent testing, open `playtest.project.yaml` instead. It uses:

```yaml
plugin: project://playtest.luau#SkylineStrikePlaytestPlugin
```

That entry exposes the `skyline-strike.main` Playtest interface and pauses the
fixed clock between agent-controlled steps by design.

The first playable slice contains a scrolling star layer, deterministic enemy
waves, player and enemy projectiles, circular hit detection, a dreadnought
boss, screen shake, explosions, HUD, victory/defeat states, and structured
Playtest observations.
