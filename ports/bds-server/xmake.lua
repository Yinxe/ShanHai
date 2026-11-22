-- Dedicated-server (BDS / LeviLamina) build of mountains&seas.
--
-- This port shares the SDK-free terrain kernel and the structure runtime with
-- the Windows client, but registers a single BDS host instead of a client
-- adapter. Build it only with LeviLamina's server configuration: the client
-- package pins a different Minecraft image and its symbols are absent here.
--
-- The relative dependency paths tolerate a checkout beneath non-ASCII parents.
set_targetdir(path.join(get_config("builddir") or "build", "$(plat)/$(arch)/$(mode)"))
add_rules("mode.debug", "mode.release")
includes("../../build-support/game-line.lua")
includes("../../build-support/structure-sdk.lua")
if get_config("game_line") ~= "26.20" then
    add_defines("SHANHAI_MC_26_51")
end
includes("../../src/kernel")
add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")
add_requires("levilamina " .. (get_config("game_line") == "26.20" and "26.20.7" or "26.51.9"),
    {configs = {target_type = "server"}})
add_requires("levibuildscript")
set_runtimes("MD")
set_languages("c++20")
set_toolchains("clang-cl")

target("karma-worldgen-bds")
    on_load(shanhai_structure_sdk_overlay)
    add_deps("terrain-kernel")
    set_kind("shared")
    set_symbols("debug")
    add_rules("@levibuildscript/linkrule")
    add_packages("levilamina")
    -- KARMA_BDS_HOST suppresses both modules' LL_REGISTER_MOD so host.cpp owns
    -- the single ll_mod_load the loader expects.
    add_defines("KARMA_BDS_HOST", "NOMINMAX", "UNICODE")
    add_cxflags("/EHa", "/utf-8", "/fp:precise", "/arch:AVX2", "/clang:-ffp-contract=off",
        "-Wno-invalid-offsetof", {tools = {"clang_cl"}})
    add_includedirs("../../src")
    add_files("host.cpp", "../../src/Worldgen.cpp", "../../experiments/vanilla-structure-runtime/lab.cpp",
        "../../src/MemoryOperators.cpp")
    -- bcrypt: SHA-256 for runtime data identity; BDS links the same import.
    add_syslinks("bcrypt")
target_end()

-- Offline SDK-free regression: the terrain kernel must reproduce the same
-- chunk fingerprints on every host, server included.
target("bds-kernel-check")
    add_deps("terrain-kernel")
    set_default(false)
    set_kind("binary")
    add_cxflags("/EHsc", "/utf-8", "/fp:precise", "/arch:AVX2", "/clang:-ffp-contract=off",
        {tools = {"clang_cl"}})
    add_includedirs("../../src")
    add_files("../../tests/native_cli.cpp")
target_end()

