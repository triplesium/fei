# Reading game observations

Start with the descriptions returned by `play_interfaces`. Determine coordinate orientation, world versus screen units, simulation timestep, action duration, and the actual success/failure fields. Do not infer collision bounds from sprite dimensions or interpret a neutral count as a terminal condition.

For spatial games, check whether observations expose:

- Player position, speed, movement bounds, hit shape, health, and temporary immunity or cooldowns needed to judge an action.
- Targets with identities, positions, types, health, and relevant motion.
- Threat positions, velocities, hit shapes, ownership/team, and damage.
- Whether collections include offscreen/inactive objects, whether they are truncated, and what counts include. Check ordering and ID lifetime guarantees.

Use the declared collection representation: JSON objects become keyed Luau tables, so iterate maps with `pairs`; do not use array length or assume ordering. Empty maps can be `{}`. Do not reuse an entity ID after despawn or restart unless the contract explicitly guarantees that identity.

For constant-velocity threats, `position + velocity * simulatedSeconds` estimates future position. Incorporate player movement and collision radii before deciding that a path is safe. Do not extend that assumption to homing, bouncing, or accelerating objects without evidence. LLM wall-clock latency is not simulation time in a paused Playtest session.

If a needed field is absent, use a capture or a narrow advertised read-only inspection. State what remains uncertain; do not silently invent threat data or modify the observation interface during a play-only task. A request to improve the interface is a separate development task.

Use before/after observations to verify the intended mechanic. A completed request with an unchanged player can still indicate a stopped clock, a blocked move, an invalid strategy, or an ended game. Inspect the relevant state and logs. Capture at useful points, such as the initial scene, a suspected rendering bug, or the final outcome, instead of filling model context with every frame.
