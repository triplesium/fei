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

## Agent observations

`play_interfaces` includes descriptions on every action and observation field.
Coordinates use world units: positive x is right, positive y is up. Velocities
are world units per simulated second; one fixed tick is 1/60 second. Capturing
or observing does not advance the simulation.

- `player` contains center position, maximum movement speed, circular hit radius,
  remaining invincibility and shot cooldown in seconds, and inclusive movement
  `bounds` for its center.
- `enemy_targets` maps entity ID strings to all living active fighters and the
  boss, with position, type, current/max health, and hit radius.
- `hostile_projectiles` maps entity ID strings to all damaging enemy shots,
  with position, constant velocity, hit radius, and damage. Predict a shot's
  position after `t` seconds with `x + velocity_x * t`, `y + velocity_y * t`.
- Both maps include offscreen objects and are not truncated. Empty maps are
  `{}`. IDs identify an entity only during its lifetime in the current run;
  do not reuse them across restarts. Map iteration order is unspecified. Luau
  controllers can iterate with `pairs(ctx.observation.hostile_projectiles)`.
- Existing `enemies` and `projectiles` counts remain available. `projectiles`
  includes both friendly and hostile shots; it is not the size of the hostile
  map. `boss_health` starts at 28 even before the boss spawns, so check
  `boss_spawned` and the target map when locating it.

Movement input is normalized if its combined length exceeds 1 and clamped to
the player's bounds. Player shots travel right; firing has no ammunition cost.
Enemy bodies currently cause no contact damage. Projectile collisions use the
sum of the two circular hit radii, not sprite dimensions. Positive
`invincible_seconds` prevents player damage from enemy shots. Stop gameplay
actions when `won` or `lost` becomes true.
