// BDS (LeviLamina server) host: one DLL, one ll_mod_load, two modules.
//
// The Windows client port coordinates the terrain generator and the structure
// runtime from a single client adapter. A dedicated server has no such
// adapter, and a native mod may only export one ll_mod_load, so this
// translation unit owns registration and drives both modules in order.
//
// Both modules keep their production server paths (plugins/karma-worldgen and
// worlds/<name>); nothing here reinterprets their data.
#include "BdsHost.h"
#include "ReleaseVersions.h"
#include "ll/api/Versions.h"
#include "ll/api/data/Version.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/GamingStatus.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
// The terrain data format is pinned per game line; a build-suffix mismatch is
// a warning, a game-line mismatch is fatal. Refusing an unknown ABI here is
// what keeps a bad DLL from writing chunks with the wrong layout.
constexpr char SupportedGameLine[] =
#ifdef SHANHAI_MC_26_51
    "1.26.51";
#else
    "1.26.20";
#endif

std::string gameLine(std::string const& version) {
    // "1.26.51.1+hash" -> "1.26.51"
    auto const first = version.find('.');
    if (first == std::string::npos) return {};
    auto const second = version.find('.', first + 1);
    if (second == std::string::npos) return {};
    auto const third = version.find_first_of(".+-", second + 1);
    return version.substr(0, third == std::string::npos ? version.size() : third);
}

class Mod {
public:
    static Mod& getInstance() {
        static Mod m;
        return m;
    }
    ll::mod::NativeMod& self = *ll::mod::NativeMod::current();

    bool load() {
        auto& logger = self.getLogger();
        auto const version = ll::getGameVersion().to_string();
        auto const line = gameLine(version);
        logger.info("mountains&seas (BDS) {} 正在加载，游戏版本 {}，地形内核 {}", shanhai_release::Terrain, version,
                    shanhai_release::SafetyRevision);
        if (line != SupportedGameLine) {
            logger.error("此 DLL 面向 {}，当前服务端为 {}；请安装匹配的游戏线版本", SupportedGameLine, version);
            return false;
        }
        // terrainLoad() stops the process on a bad data identity to protect
        // saved chunks; anything else is reported here so the operator sees it.
        try {
            if (!shanhai_bds::terrainLoad()) {
                logger.error("地形模块加载失败；详见 plugins/karma-worldgen 诊断文件");
                return false;
            }
        } catch (std::exception const& e) {
            logger.error("地形模块加载异常：{}", e.what());
            return false;
        }
        try {
            if (!shanhai_bds::structuresLoad()) {
                // Terrain without structures is a supported configuration: a
                // world may ship terrain data only. Report it and keep
                // generating terrain rather than failing the server start.
                logger.warn("结构模块未启用（缺少 plugins/vanilla-structure-lab/config.json 或版本不符）；地形继续运行");
                return true;
            }
        } catch (std::exception const& e) {
            logger.warn("结构模块加载异常，仅地形生效：{}", e.what());
            return true;
        }
        return true;
    }

    bool enable() {
        auto& logger = self.getLogger();
        try {
            if (!shanhai_bds::terrainEnable()) {
                logger.error("地形模块启用失败");
                return false;
            }
        } catch (std::exception const& e) {
            logger.error("地形模块启用异常：{}", e.what());
            return false;
        }
        // A structure failure must never take terrain down with it: the two
        // modules are independent and terrain-only is a supported world.
        try {
            structuresReady = shanhai_bds::structuresEnable();
            if (!structuresReady) logger.warn("结构模块启用失败；仅地形生效");
        } catch (std::exception const& e) {
            // The structure runtime installs its generator hooks before any
            // world exists, which a dedicated server has already done by the
            // time enable() runs. Terrain is unaffected, so keep serving.
            logger.warn("结构模块未生效（服务端需在开服前装配钩子）：{}", e.what());
            structuresReady = false;
        }
        logger.info("mountains&seas (BDS) 已启用：地形{}, 结构{}", "开", structuresReady ? "开" : "关");
        return true;
    }

    bool disable() {
        // Order matters: structures hook world generation and read terrain
        // plans, so they must release before the terrain session closes.
        if (structuresReady) shanhai_bds::structuresDisable();
        shanhai_bds::terrainDisable();
        return true;
    }

    bool unload() {
        auto const status = ll::getGamingStatus();
        if (status == ll::GamingStatus::Running || status == ll::GamingStatus::Starting) {
            self.getLogger().error("服务端运行中不能卸载 mountains&seas；请正常停服后更新");
            return false;
        }
        if (structuresReady) shanhai_bds::structuresUnload();
        return shanhai_bds::terrainUnload();
    }

private:
    bool structuresReady = false;
};
} // namespace

LL_REGISTER_MOD(Mod, Mod::getInstance());
