local core = require("@entisium/core")

local function require_image(assets: ResRW<AssetServer>, path: string)
    local image = assets:load(Image, path)
    if not assets:is_loaded(image) then
        error(assets:load_error(image) or (path .. " did not load"))
    end
    return image
end

export type GameState = {
    initialized: i32,
    phase: i32,
    elapsed: f32,
    score: i32,
    kills: i32,
    player_health: i32,
    boss_health: i32,
    boss_spawned: i32,
    spawn_timer: f32,
    spawned: i32,
    enemy_count: i32,
    shake: f32,
}

export type GameAssets = {
    background: Handle<Image>,
    player: Handle<Image>,
    plasma: Handle<Image>,
    enemy: Handle<Image>,
    boss: Handle<Image>,
    explosion: Handle<Image>,
}

export type Health = {
    current: f32,
    maximum: f32,
}

export type Projectile = {
    team: i32,
    damage: f32,
    velocity_x: f32,
    velocity_y: f32,
    radius: f32,
}

export type Effect = {
    age: f32,
    duration: f32,
}


export type Star = {
    speed: f32,
}

export type GameCamera = {}

export function scroll_stars(
    time: ResRO<core.FixedTime>,
    stars: Query<Write<core.Transform2d>, Read<Star>>
)
    local delta = time:delta()
    for transform, star in stars do
        transform.position.x -= star.speed * delta
        if transform.position.x < -9.1 then
            transform.position.x += 18.2
        end
    end
end

export function update_camera(
    time: ResRO<core.FixedTime>,
    state: ResRW<GameState>,
    cameras: Query<Write<core.Transform2d>, With<GameCamera>>
)
    local delta = time:delta()
    for transform in cameras do
        if state.shake > 0.0 then
            transform.position.x = math.sin(state.elapsed * 97.0) * state.shake
            transform.position.y = math.cos(state.elapsed * 83.0) * state.shake
            state.shake = math.max(0.0, state.shake - delta * 2.8)
        else
            transform.position.x = 0.0
            transform.position.y = 0.0
        end
    end
end


export function spawn_explosion(commands, assets, position, size)
    local image = require_image(assets, "images/explosion.png")
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(size, size),
            layer = 8,
        },
        core.Transform2d.new {
            position = Vector2.new(position.x, position.y),
            scale = Vector2.new(0.25, 0.25),
        },
        Effect.new {duration = 0.42}
    )
end

export function spawn_projectile(
    commands,
    assets,
    position,
    team,
    velocity_x,
    velocity_y,
    damage,
    size
)
    local image = require_image(assets, "images/plasma.png")
    local enemy_shot = team ~= 0
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(size, size * 0.36),
            flip_x = enemy_shot,
            color = if enemy_shot
                then Color4F.new(1.0, 0.18, 0.08, 0.95)
                else Color4F.new(0.35, 0.9, 1.0, 1.0),
            layer = 5,
        },
        core.Transform2d.new {
            position = Vector2.new(position.x, position.y),
        },
        Projectile.new {
            team = team,
            damage = damage,
            velocity_x = velocity_x,
            velocity_y = velocity_y,
            radius = size * 0.22,
        }
    )
end


export type ControlState = {
    keyboard_x: f32,
    keyboard_y: f32,
    keyboard_fire: i32,
    agent_x: f32,
    agent_y: f32,
    agent_fire: i32,
    agent_active: i32,
}

export type Player = {
    speed: f32,
    fire_timer: f32,
    invincible: f32,
}

export function read_keyboard(
    keys: ResRO<KeyInput>,
    control: ResRW<ControlState>
)
    control.keyboard_x = 0.0
    control.keyboard_y = 0.0
    control.keyboard_fire = 0

    if control.agent_active ~= 0 then
        return
    end

    if keys:pressed(KeyCode.Left) or keys:pressed(KeyCode.A) then
        control.keyboard_x -= 1.0
    end
    if keys:pressed(KeyCode.Right) or keys:pressed(KeyCode.D) then
        control.keyboard_x += 1.0
    end
    if keys:pressed(KeyCode.Down) or keys:pressed(KeyCode.S) then
        control.keyboard_y -= 1.0
    end
    if keys:pressed(KeyCode.Up) or keys:pressed(KeyCode.W) then
        control.keyboard_y += 1.0
    end
    if keys:pressed(KeyCode.Space) or keys:pressed(KeyCode.Z) then
        control.keyboard_fire = 1
    end
end

export function move_player(
    time: ResRO<core.FixedTime>,
    state: ResRW<GameState>,
    control: ResRO<ControlState>,
    players: Query<Write<core.Transform2d>, Write<Player>>
)
    if state.phase ~= 0 then
        return
    end

    local x = math.clamp(control.keyboard_x + control.agent_x, -1.0, 1.0)
    local y = math.clamp(control.keyboard_y + control.agent_y, -1.0, 1.0)
    local length = math.sqrt(x * x + y * y)
    if length > 1.0 then
        x /= length
        y /= length
    end

    local delta = time:delta()
    for transform, player in players do
        transform.position.x = math.clamp(
            transform.position.x + x * player.speed * delta,
            -8.0,
            2.7
        )
        transform.position.y = math.clamp(
            transform.position.y + y * player.speed * delta,
            -4.25,
            4.25
        )
        player.fire_timer = math.max(0.0, player.fire_timer - delta)
        player.invincible = math.max(0.0, player.invincible - delta)
    end
end

export function fire_player(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRO<GameState>,
    control: ResRO<ControlState>,
    players: Query<Read<core.Transform2d>, Write<Player>>
)
    if state.phase ~= 0 or
        (control.keyboard_fire == 0 and control.agent_fire == 0) then
        return
    end

    for transform, player in players do
        if player.fire_timer <= 0.0 then
            spawn_projectile(
                commands,
                assets,
                Vector2.new(transform.position.x + 1.0, transform.position.y),
                0,
                10.8,
                0.0,
                1.0,
                0.82
            )
            player.fire_timer = 0.145
        end
    end
end


export type Enemy = {
    kind: i32,
    speed: f32,
    base_y: f32,
    phase: f32,
    fire_timer: f32,
    active: i32,
}

local function spawn_enemy(commands, assets, index, y)
    local image = require_image(assets, "images/enemy.png")
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.45, 0.92),
            layer = 3,
        },
        core.Transform2d.new {
            position = Vector2.new(9.6 + (index % 3) * 0.65, y),
        },
        Enemy.new {
            kind = 0,
            speed = 1.55 + (index % 4) * 0.13,
            base_y = y,
            phase = index * 0.71,
            fire_timer = 0.7 + (index % 3) * 0.38,
            active = 1,
        },
        Health.new {current = 3.0, maximum = 3.0}
    )
end

local function spawn_boss(commands, assets)
    local image = require_image(assets, "images/boss.png")
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(4.6, 3.05),
            layer = 4,
        },
        core.Transform2d.new {
            position = Vector2.new(10.8, 0.0),
        },
        Enemy.new {
            kind = 1,
            speed = 0.72,
            base_y = 0.0,
            phase = 0.0,
            fire_timer = 1.1,
            active = 1,
        },
        Health.new {current = 28.0, maximum = 28.0}
    )
end

export function spawn_wave(
    commands: Commands,
    assets: ResRW<AssetServer>,
    time: ResRO<core.FixedTime>,
    state: ResRW<GameState>
)
    if state.phase ~= 0 then
        return
    end

    local delta = time:delta()
    state.elapsed += delta
    state.spawn_timer -= delta

    if state.boss_spawned == 0 and state.spawned < 16 and
        state.spawn_timer <= 0.0 then
        local lane = state.spawned % 7
        local y = -3.45 + lane * 1.15
        spawn_enemy(commands, assets, state.spawned, y)
        state.spawned += 1
        state.enemy_count += 1
        state.spawn_timer = 0.62
    end

    if state.boss_spawned == 0 and
        (state.elapsed >= 16.0 or state.kills >= 14) then
        spawn_boss(commands, assets)
        state.boss_spawned = 1
        state.enemy_count += 1
    end
end

export function update_enemies(
    commands: Commands,
    assets: ResRW<AssetServer>,
    time: ResRO<core.FixedTime>,
    state: ResRW<GameState>,
    enemies: Query<
        Entity,
        Write<core.Transform2d>,
        Write<Enemy>,
        Read<Health>
    >
)
    if state.phase ~= 0 then
        return
    end

    local delta = time:delta()
    for entity, transform, enemy, health in enemies do
        if enemy.active == 0 then
            continue
        end

        enemy.fire_timer -= delta
        if enemy.kind == 0 then
            transform.position.x -= enemy.speed * delta
            transform.position.y = enemy.base_y +
                math.sin(state.elapsed * 2.25 + enemy.phase) * 0.48

            if enemy.fire_timer <= 0.0 and transform.position.x < 7.8 then
                spawn_projectile(
                    commands,
                    assets,
                    Vector2.new(
                        transform.position.x - 0.72,
                        transform.position.y
                    ),
                    1,
                    -4.7,
                    math.sin(enemy.phase) * 0.42,
                    1.0,
                    0.72
                )
                enemy.fire_timer = 2.0 + (entity % 5) * 0.16
            end

            if transform.position.x < -9.8 then
                enemy.active = 0
                state.enemy_count -= 1
                commands:entity(entity):despawn()
            end
        else
            if transform.position.x > 5.9 then
                transform.position.x -= enemy.speed * delta
            else
                enemy.phase += delta * 0.72
                transform.position.y = math.sin(enemy.phase) * 1.55
            end
            state.boss_health = math.max(0, math.floor(health.current + 0.5))

            if enemy.fire_timer <= 0.0 and transform.position.x <= 6.3 then
                for shot = -2, 2 do
                    spawn_projectile(
                        commands,
                        assets,
                        Vector2.new(
                            transform.position.x - 2.0,
                            transform.position.y + shot * 0.23
                        ),
                        1,
                        -4.5,
                        shot * 0.72,
                        1.0,
                        0.78
                    )
                end
                enemy.fire_timer = 1.25
            end
        end
    end
end


export function move_projectiles(
    commands: Commands,
    time: ResRO<core.FixedTime>,
    projectiles: Query<
        Entity,
        Write<core.Transform2d>,
        Read<Projectile>
    >
)
    local delta = time:delta()
    for entity, transform, projectile in projectiles do
        transform.position.x += projectile.velocity_x * delta
        transform.position.y += projectile.velocity_y * delta
        if transform.position.x < -10.5 or transform.position.x > 10.5 or
            math.abs(transform.position.y) > 6.0 then
            commands:entity(entity):despawn()
        end
    end
end

export function detect_hits(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<GameState>,
    projectiles: Query<
        Entity,
        Read<core.Transform2d>,
        Write<Projectile>
    >,
    enemies: Query<
        Entity,
        Read<core.Transform2d>,
        Write<Enemy>,
        Write<Health>
    >,
    players: Query<
        Read<core.Transform2d>,
        Write<Player>,
        Write<Health>
    >
)
    if state.phase ~= 0 then
        return
    end

    for projectile_entity, projectile_transform, projectile in projectiles do
        if projectile.damage <= 0.0 then
            continue
        end

        if projectile.team == 0 then
            for enemy_entity, enemy_transform, enemy, health in enemies do
                if enemy.active == 0 then
                    continue
                end
                local radius = if enemy.kind == 1 then 1.35 else 0.47
                local dx = projectile_transform.position.x -
                    enemy_transform.position.x
                local dy = projectile_transform.position.y -
                    enemy_transform.position.y
                local hit_radius = radius + projectile.radius
                if dx * dx + dy * dy <= hit_radius * hit_radius then
                    health.current -= projectile.damage
                    projectile.damage = 0.0
                    commands:entity(projectile_entity):despawn()
                    state.shake = math.max(state.shake, 0.08)

                    if health.current <= 0.0 then
                        enemy.active = 0
                        local blast_size = if enemy.kind == 1 then 4.1 else 1.45
                        spawn_explosion(
                            commands,
                            assets,
                            enemy_transform.position,
                            blast_size
                        )
                        commands:entity(enemy_entity):despawn()
                        state.enemy_count -= 1
                        state.kills += 1
                        state.score += if enemy.kind == 1 then 5000 else 250
                        state.shake = if enemy.kind == 1 then 0.85 else 0.22
                        if enemy.kind == 1 then
                            state.boss_health = 0
                            state.phase = 1
                        end
                    end
                    break
                end
            end
        else
            for player_transform, player, health in players do
                if player.invincible > 0.0 then
                    continue
                end
                local dx = projectile_transform.position.x -
                    player_transform.position.x
                local dy = projectile_transform.position.y -
                    player_transform.position.y
                local hit_radius = 0.34 + projectile.radius
                if dx * dx + dy * dy <= hit_radius * hit_radius then
                    health.current -= projectile.damage
                    projectile.damage = 0.0
                    player.invincible = 0.85
                    state.player_health = math.max(
                        0,
                        math.floor(health.current + 0.5)
                    )
                    state.shake = 0.42
                    commands:entity(projectile_entity):despawn()
                    spawn_explosion(
                        commands,
                        assets,
                        player_transform.position,
                        0.85
                    )
                    if health.current <= 0.0 then
                        state.phase = 2
                    end
                    break
                end
            end
        end
    end
end

export function animate_effects(
    commands: Commands,
    time: ResRO<core.FixedTime>,
    effects: Query<
        Entity,
        Write<core.Transform2d>,
        Write<Sprite>,
        Write<Effect>
    >
)
    local delta = time:delta()
    for entity, transform, sprite, effect in effects do
        effect.age += delta
        local progress = math.clamp(effect.age / effect.duration, 0.0, 1.0)
        local scale = 0.25 + math.sin(progress * math.pi) * 0.95
        transform.scale = Vector2.new(scale, scale)
        transform.rotation += 150.0 * delta
        sprite.color.a = 1.0 - progress
        if progress >= 1.0 then
            commands:entity(entity):despawn()
        end
    end
end


export function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    images: ResRW<GameAssets>,
    state: ResRW<GameState>
)
    if state.initialized ~= 0 then
        return
    end

    images.background = require_image(assets, "images/cloudsea.png")
    images.player = require_image(assets, "images/player.png")
    images.plasma = require_image(assets, "images/plasma.png")
    images.enemy = require_image(assets, "images/enemy.png")
    images.boss = require_image(assets, "images/boss.png")
    images.explosion = require_image(assets, "images/explosion.png")

    local background = images.background
    local player_image = images.player
    local plasma = images.plasma

    commands:spawn(
        Camera2d.new {
            vertical_size = 10.0,
            clear_color = Color4F.new(0.012, 0.025, 0.055, 1.0),
        },
        core.Transform2d.new(),
        GameCamera.new()
    )

    commands:spawn(
        Sprite.new {
            image = background,
            size = Vector2.new(17.78, 10.0),
            color = Color4F.new(0.72, 0.82, 1.0, 1.0),
            layer = -20,
        },
        core.Transform2d.new()
    )

    for index = 1, 28 do
        local row = index % 9
        local column = math.floor(index / 9)
        commands:spawn(
            Sprite.new {
                image = plasma,
                size = Vector2.new(0.12 + (index % 4) * 0.035, 0.025),
                color = Color4F.new(0.65, 0.9, 1.0, 0.42),
                layer = -5,
            },
            core.Transform2d.new {
                position = Vector2.new(
                    -8.7 + ((index * 1.93) % 17.4),
                    -4.35 + row * 1.03 + column * 0.11
                ),
            },
            Star.new {speed = 0.35 + (index % 5) * 0.22}
        )
    end

    local player = commands:spawn(
        Sprite.new {
            image = player_image,
            size = Vector2.new(1.85, 0.93),
            layer = 6,
        },
        core.Transform2d.new {
            position = Vector2.new(-6.2, 0.0),
        },
        Player.new {speed = 5.7},
        Health.new {current = 8.0, maximum = 8.0}
    )

    state.initialized = player:id()
    state.phase = 0
    state.player_health = 8
    state.boss_health = 28
    state.spawn_timer = 0.8
end
