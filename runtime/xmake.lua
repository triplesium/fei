includes("browser")

if not is_plat("wasm") then
    includes("native")
end
