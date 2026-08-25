# Skyline Strike

`Skyline Strike` is a 16:9 horizontal shoot-em-up implemented entirely in
Luau gameplay scripts. Open this directory in the Entisium Editor and press
Play.

Controls:

- Move with WASD or the arrow keys.
- Fire with Space or Z.

The default project entry runs continuously for normal play. For deterministic
agent testing, temporarily change `project.yaml` to use:

```yaml
plugin: project://scripts/playtest_main.luau#SkylineStrikePlaytestPlugin
```

That entry exposes the `skyline-strike.main` Playtest interface and pauses the
fixed clock between agent-controlled steps by design.

The first playable slice contains a scrolling star layer, deterministic enemy
waves, player and enemy projectiles, circular hit detection, a dreadnought
boss, screen shake, explosions, HUD, victory/defeat states, and structured
Playtest observations.
