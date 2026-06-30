local function add_scaled(position, direction, amount)
    position.x = position.x + direction.x * amount
    position.y = position.y + direction.y * amount
    position.z = position.z + direction.z * amount
end

function update_camera(args)
    local input = args.input
    local time = args.time
    local app = args.app

    local move_speed = 10.0
    local rotate_speed = 90.0
    local move_step = move_speed * time:delta()
    local rotate_step = rotate_speed * time:delta()

    for camera in args.cameras:iter() do
        local transform = camera.transform

        if input:pressed(KeyCode.W) then
            add_scaled(transform.position, transform:forward(), move_step)
        end
        if input:pressed(KeyCode.S) then
            add_scaled(transform.position, transform:forward(), -move_step)
        end
        if input:pressed(KeyCode.A) then
            add_scaled(transform.position, transform:right(), -move_step)
        end
        if input:pressed(KeyCode.D) then
            add_scaled(transform.position, transform:right(), move_step)
        end
        if input:pressed(KeyCode.Space) then
            add_scaled(transform.position, transform:up(), move_step)
        end
        if input:pressed(KeyCode.LeftControl) then
            add_scaled(transform.position, transform:up(), -move_step)
        end

        if input:pressed(KeyCode.Up) then
            transform:rotate_x(rotate_step)
        end
        if input:pressed(KeyCode.Down) then
            transform:rotate_x(-rotate_step)
        end
        if input:pressed(KeyCode.Left) then
            transform:rotate_y(rotate_step)
        end
        if input:pressed(KeyCode.Right) then
            transform:rotate_y(-rotate_step)
        end

        if input:just_pressed(KeyCode.P) then
            local position = transform.position
            print(position.x, position.y, position.z)
        end
    end
    if input:pressed(KeyCode.Escape) then
        app.should_stop = true
    end
end

system {
    name = "camera_control",
    run = update_camera,
    schedule = MainSchedules.Update,
    params = {
        input = res.read(KeyInput),
        time = res.read(Time),
        app = res.write(AppStates),
        cameras = query {
            transform = query.write(Transform3d),
            query.with(Camera3d),
        },
    },
}
