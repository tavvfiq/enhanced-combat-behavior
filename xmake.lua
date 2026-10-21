-- set minimum xmake version
set_xmakever("3.0.0")

-- includes
local commonlibsse_ng_dir = os.getenv("COMMONLIBSSE_NG_DIR") or "D:/Modding/commonlibsse-xmake/CommonLibSSE-NG"
includes(path.join(commonlibsse_ng_dir, "xmake.lua"))
includes("extern/styyx-util")

-- set project
set_project("EnhancedCombatAI")
set_version("1.10.2")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", { outputdir = ".vscode", lsp = "clangd" })

-- require packages
add_requires("simpleini")
add_requires("spdlog v1.16.0", { configs = { header_only = false, wchar = true, std_format = true } })

-- targets
target("EnhancedCombatAI")
-- add dependencies to target
add_deps("commonlibsse-ng")
add_deps("styyx-util")
add_packages("simpleini")
if has_config("skyrim_se") and has_config("skyrim_ae") then
    set_targetdir("/build/SkyrimSEAE/skse/plugins")
elseif has_config("skyrim_ae") then
    set_targetdir("/build/SkyrimAE/skse/plugins")
else
    set_targetdir("/build/SkyrimSE/skse/plugins")
end

-- add commonlibsse-ng plugin
add_rules("commonlibsse-ng.plugin", {
    name = "EnhancedCombatAI",
    author = "tavvfiq",
    description = "enhanced combat ai for skyrim"
})


-- add src files
add_files("src/**.cpp")
add_headerfiles("include/**.h")
add_includedirs("include")
set_pcxxheader("include/pch.h")
