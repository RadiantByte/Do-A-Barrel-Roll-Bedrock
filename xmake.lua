add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("client", "server")
option_end()

add_requires("levilamina 26.51.6", {configs = {target_type = get_config("target_type")}})

add_requires("levibuildscript 0.6.1")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("DoABarrelRoll")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modName = "DoABarrelRoll", modVersion = "0.1.0-mc26.51", modPlatform = get_config("target_type")})
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_files("src/mod/*.cpp", "src/mod/core/*.cpp", "src/mod/net/*.cpp")
    if get_config("target_type") == "server" then
        add_defines("DABR_SERVER")
        add_files("src/mod/server/*.cpp")
    else
        add_defines("DABR_CLIENT")
        add_files("src/mod/hooks/*.cpp")
    end
