task("tidy")
    on_run(function ()
        local option = import("core.base.option")
        import("tasks.tidy", {
            rootdir = path.join(os.projectdir(), "tools")
        }).run({
            jobs = option.get("jobs"),
            targets = option.get("targets"),
            verbose = option.get("verbose")
        })
    end)

    set_menu {
        usage = "xmake tidy [options] [targets]",
        description = "Run clang-tidy for project sources and headers.",
        options = {
            {"j", "jobs", "kv", tostring(os.default_njob()), "Set the number of parallel clang-tidy jobs."},
            {nil, "targets", "vs", nil, "Run clang-tidy for the given targets."}
        }
    }

task("format")
    on_run(function ()
        local option = import("core.base.option")
        import("tasks.format", {
            rootdir = path.join(os.projectdir(), "tools")
        }).run({
            check = option.get("check"),
            jobs = option.get("jobs"),
            targets = option.get("targets"),
            verbose = option.get("verbose")
        })
    end)

    set_menu {
        usage = "xmake format [options] [targets]",
        description = "Run clang-format for project sources and headers.",
        options = {
            {nil, "check", "k", nil, "Check formatting without modifying files."},
            {"j", "jobs", "kv", tostring(os.default_njob()), "Set the number of parallel clang-format jobs."},
            {nil, "targets", "vs", nil, "Run clang-format for the given targets."}
        }
    }

task("profile")
    on_run(function()
        local option = import("core.base.option")
        local target = option.get("target")
        local seconds = option.get("seconds")
        local frames = option.get("frames")
        local top_count = tonumber(option.get("top") or "10")

        if not target then
            raise("missing target, usage: xmake profile <target> [--seconds=N|--frames=N]")
        end
        if not top_count or top_count < 1 then
            raise("--top must be a positive integer")
        end
        top_count = math.floor(top_count)
        if seconds and frames then
            raise("use only one of --seconds or --frames")
        end
        if not seconds and not frames then
            seconds = "10"
        end

        if seconds then
            os.setenv("FEI_EXIT_AFTER_SECONDS", tostring(seconds))
            os.setenv("FEI_EXIT_AFTER_FRAMES", "")
        end
        if frames then
            os.setenv("FEI_EXIT_AFTER_FRAMES", tostring(frames))
            os.setenv("FEI_EXIT_AFTER_SECONDS", "")
        end

        os.vrunv("xmake", {"run", target})

        local output_dir = path.join(os.projectdir(), "build", "profile", "latest")

        local function split_csv_line(line)
            local fields = {}
            local field = {}
            local in_quotes = false
            local index = 1

            while index <= #line do
                local char = line:sub(index, index)
                if in_quotes then
                    if char == '"' then
                        local next_char = line:sub(index + 1, index + 1)
                        if next_char == '"' then
                            table.insert(field, '"')
                            index = index + 1
                        else
                            in_quotes = false
                        end
                    else
                        table.insert(field, char)
                    end
                else
                    if char == '"' then
                        in_quotes = true
                    elseif char == "," then
                        table.insert(fields, table.concat(field))
                        field = {}
                    else
                        table.insert(field, char)
                    end
                end
                index = index + 1
            end

            table.insert(fields, table.concat(field))
            return fields
        end

        local function read_csv(file)
            local rows = {}
            local stream = io.open(file, "r")
            if not stream then
                return rows
            end

            local header_line = stream:read("*l")
            if not header_line then
                stream:close()
                return rows
            end

            local headers = split_csv_line(header_line)
            for line in stream:lines() do
                if line ~= "" then
                    local values = split_csv_line(line)
                    local row = {}
                    for index, header in ipairs(headers) do
                        row[header] = values[index] or ""
                    end
                    table.insert(rows, row)
                end
            end

            stream:close()
            return rows
        end

        local function number_field(row, field)
            return tonumber(row[field] or "") or 0
        end

        local function format_ms(value)
            if value >= 100 then
                return string.format("%.1fms", value)
            end
            if value >= 10 then
                return string.format("%.2fms", value)
            end
            if value >= 1 then
                return string.format("%.3fms", value)
            end
            return string.format("%.4fms", value)
        end

        local function sorted_by_self_time(rows)
            table.sort(rows, function(left, right)
                local left_self = number_field(left, "self_ms")
                local right_self = number_field(right, "self_ms")
                if left_self == right_self then
                    return number_field(left, "total_ms") > number_field(right, "total_ms")
                end
                return left_self > right_self
            end)
            return rows
        end

        local function print_systems(rows)
            if #rows == 0 then
                print("Top systems by self time: <no data>")
                return
            end

            print("Top systems by self time")
            sorted_by_self_time(rows)
            for index = 1, math.min(top_count, #rows) do
                local row = rows[index]
                local label = row.system or ""
                if row.schedule and row.schedule ~= "" then
                    label = string.format("%s / %s", row.schedule, label)
                end
                print(
                    string.format(
                        "%2d. %-56s self %9s total %9s count %s max %s",
                        index,
                        label,
                        format_ms(number_field(row, "self_ms")),
                        format_ms(number_field(row, "total_ms")),
                        row.count or "0",
                        format_ms(number_field(row, "max_ms"))
                    )
                )
            end
        end

        local function print_zones(rows)
            if #rows == 0 then
                print("Top zones by self time: <no data>")
                return
            end

            print("Top zones by self time")
            sorted_by_self_time(rows)
            for index = 1, math.min(top_count, #rows) do
                local row = rows[index]
                print(
                    string.format(
                        "%2d. %-56s self %9s total %9s count %s max %s",
                        index,
                        row.zone or "",
                        format_ms(number_field(row, "self_ms")),
                        format_ms(number_field(row, "total_ms")),
                        row.count or "0",
                        format_ms(number_field(row, "max_ms"))
                    )
                )
            end
        end

        local function percentile(sorted_values, percentile_value)
            if #sorted_values == 0 then
                return 0
            end

            local rank = math.ceil(#sorted_values * percentile_value)
            rank = math.max(1, math.min(rank, #sorted_values))
            return sorted_values[rank]
        end

        local function print_frames(rows)
            if #rows == 0 then
                print("Frames: <no data>")
                return
            end

            local values = {}
            local total = 0
            local max_value = 0
            for _, row in ipairs(rows) do
                local value = number_field(row, "duration_ms")
                total = total + value
                max_value = math.max(max_value, value)
                table.insert(values, value)
            end
            table.sort(values)

            print(
                string.format(
                    "Frames: count %d, mean %s, p50 %s, p95 %s, max %s",
                    #values,
                    format_ms(total / #values),
                    format_ms(percentile(values, 0.50)),
                    format_ms(percentile(values, 0.95)),
                    format_ms(max_value)
                )
            )
        end

        print("profile summary: %s", output_dir)
        print("")
        print_systems(read_csv(path.join(output_dir, "systems.csv")))
        print("")
        print_zones(read_csv(path.join(output_dir, "zones.csv")))
        print("")
        print_frames(read_csv(path.join(output_dir, "frames.csv")))
    end)

    set_menu {
        usage = "xmake profile [options] <target>",
        description = "Run a target for a bounded profiling capture.",
        options = {
            {nil, "seconds", "kv", nil, "Exit the app after the given number of seconds."},
            {nil, "frames", "kv", nil, "Exit the app after the given number of frames."},
            {nil, "top", "kv", "10", "Number of top systems and zones to print."},
            {nil, "target", "v", nil, "Target to run, for example sample-scene."}
        }
    }
