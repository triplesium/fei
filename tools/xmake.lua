task("reflgen")
    on_run(function ()
        local project = import("core.project.project")
        local headers = {}
        local include_dirs = {}
        for _, dep in pairs(project.target("fei-generated"):get("deps")) do
            local dep_target = project.target(dep)
            -- Collect header files
            for _, headerfile in ipairs(dep_target:headerfiles()) do
                headerfile = headerfile:replace("\\", "/")
                if not table.contains(headers, headerfile) then
                    headerfile = headerfile:replace("\\", "/")
                    table.insert(headers, headerfile)
                end
            end
            
            -- Collect all include directories for this target
            -- Get target's own include directories
            local target_includedirs = dep_target:get("includedirs")
            if target_includedirs then
                for _, incdir in ipairs(target_includedirs) do
                    incdir = incdir:replace("\\", "/")
                    if not table.contains(include_dirs, incdir) then
                        table.insert(include_dirs, incdir)
                    end
                end
            end
            
            -- Get system include directories from packages
            local target_sysincludedirs = dep_target:get("sysincludedirs") 
            if target_sysincludedirs then
                for _, incdir in ipairs(target_sysincludedirs) do
                    incdir = incdir:replace("\\", "/")
                    if not table.contains(include_dirs, incdir) then
                        table.insert(include_dirs, incdir)
                    end
                end
            end
            
            -- Get package include directories
            for _, pkg in ipairs(dep_target:orderpkgs()) do
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
            ::continue::
        end
        args = {"tools/reflgen.py", "--rootdir", "src", "-m", "cpp", "--output", "src/generated/reflgen.cpp"}
        
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

    set_menu {
        usage = "xmake reflgen",
        description = "Generate reflection metadata for the project.",
    }
