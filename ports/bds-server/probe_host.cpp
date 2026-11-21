// Minimal BDS host used to bisect a loader-time crash: registration only, no
// terrain kernel, no structure runtime, no memory operators.
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include <string>

namespace {
class Probe {
public:
    static Probe& getInstance() {
        static Probe p;
        return p;
    }
    ll::mod::NativeMod& self = *ll::mod::NativeMod::current();

    bool load() {
        self.getLogger().info("probe: load reached");
        return true;
    }
    bool enable() {
        self.getLogger().info("probe: enable reached");
        return true;
    }
    bool disable() { return true; }
    bool unload() { return true; }
};
} // namespace

LL_REGISTER_MOD(Probe, Probe::getInstance());
