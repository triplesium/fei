function on_update()
    local key_input = world:resource(KeyInput)
    local time = world:resource(Time)
    local transform = entity:component(Transform3d)
    local position = transform.position

    local move_speed = 10.0
    local rotate_speed = 90.0

    if key_input:pressed(KeyCode.W) then
        transform.position = position + transform:forward() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.S) then
        transform.position = position - transform:forward() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.A) then
        transform.position = position - transform:right() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.D) then
        transform.position = position + transform:right() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.Space) then
        transform.position = position + transform:up() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.LeftControl) then
        transform.position = position - transform:up() * move_speed * time:delta()
    end
    if key_input:pressed(KeyCode.Up) then
        transform.rotation.x = transform.rotation.x + rotate_speed * time:delta()
    end
    if key_input:pressed(KeyCode.Down) then
        transform.rotation.x = transform.rotation.x - rotate_speed * time:delta()
    end
    if key_input:pressed(KeyCode.Left) then
        transform.rotation.y = transform.rotation.y + rotate_speed * time:delta()
    end
    if key_input:pressed(KeyCode.Right) then
        transform.rotation.y = transform.rotation.y - rotate_speed * time:delta()
    end

    if key_input:just_pressed(KeyCode.P) then
        print(position.x, position.y, position.z)
    end
    if key_input:pressed(KeyCode.Escape) then
        local app_states = world:resource(AppStates)
        app_states.should_stop = true
    end
end
