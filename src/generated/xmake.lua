target("fei-generated")
    set_kind("phony")
    add_deps(
        "fei-base", 
        "fei-refl", 
        "fei-ecs", 
        "fei-app", 
        "fei-window", 
        "fei-math", 
        "fei-graphics",
        "fei-graphics-opengl",
        "fei-render2d", 
        "fei-core"
    )
    after_build(function (target)
        -- os.execv("python3", {"reflgen/main.py"})
        headers = {}
        include_dirs = {}
        
        for _, dep in ipairs(target:get("deps")) do
            local project = import("core.project.project")
            subtarget = project.target(dep)
            
            -- Collect header files
            for _, headerfile in ipairs(subtarget:headerfiles()) do
                headerfile = headerfile:replace("\\", "/")
                table.insert(headers, headerfile)
            end
            
            -- Collect all include directories for this target
            -- Get target's own include directories
            local target_includedirs = subtarget:get("includedirs")
            if target_includedirs then
                for _, incdir in ipairs(target_includedirs) do
                    -- incdir = path.absolute(incdir, subtarget:scriptdir())
                    incdir = incdir:replace("\\", "/")
                    if not table.contains(include_dirs, incdir) then
                        table.insert(include_dirs, incdir)
                    end
                end
            end
            
            -- Get system include directories from packages
            local target_sysincludedirs = subtarget:get("sysincludedirs") 
            if target_sysincludedirs then
                for _, incdir in ipairs(target_sysincludedirs) do
                    incdir = incdir:replace("\\", "/")
                    if not table.contains(include_dirs, incdir) then
                        table.insert(include_dirs, incdir)
                    end
                end
            end
            
            -- Get package include directories
            for _, pkg in ipairs(subtarget:orderpkgs()) do
                local pkgincludedirs = pkg:get("includedirs")
                if pkgincludedirs then
                    for _, incdir in ipairs(pkgincludedirs) do
                        incdir = incdir:replace("\\", "/")
                        if not table.contains(include_dirs, incdir) then
                            table.insert(include_dirs, incdir)
                        end
                    end
                end
                
                local pkgsysincludedirs = pkg:get("sysincludedirs")
                if pkgsysincludedirs then
                    for _, incdir in ipairs(pkgsysincludedirs) do
                        incdir = incdir:replace("\\", "/")
                        if not table.contains(include_dirs, incdir) then
                            table.insert(include_dirs, incdir)
                        end
                    end
                end
            end
        end
        
        args = {"tools/reflgen.py", "--rootdir", "src", "-m", "cpp", "--output", "src/generated/reflgen.hpp"}
        
        -- Add all collected include directories
        for _, incdir in ipairs(include_dirs) do
            table.insert(args, "-I")
            table.insert(args, incdir)
        end
        
        -- Add headers
        for _, header in ipairs(headers) do
            table.insert(args, header)
        end
        -- print(args)
        os.execv("python3", args)
    end)
