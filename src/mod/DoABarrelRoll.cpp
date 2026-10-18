#include "mod/DoABarrelRoll.h"

#include "ll/api/mod/RegisterHelper.h"
#include "mod/hooks/State.h"

namespace do_a_barrel_roll {

DoABarrelRoll& DoABarrelRoll::getInstance() {
    static DoABarrelRoll instance;
    return instance;
}

bool DoABarrelRoll::load() {
    getSelf().getLogger().debug("Loading...");
    namespace st = state;
    st::configPath = getSelf().getConfigDir() / "config.json";
    if (std::filesystem::exists(st::configPath)) {
        if (!st::config.load(st::configPath)) {
            getSelf().getLogger().warn("Could not parse {}, using defaults", st::configPath.string());
        }
    } else if (!st::config.save(st::configPath)) {
        getSelf().getLogger().warn("Could not write default config to {}", st::configPath.string());
    }
    return true;
}

bool DoABarrelRoll::enable() {
    getSelf().getLogger().debug("Enabling...");
    hooks::installInput();
    hooks::installCamera();
    hooks::installPlayer();
    hooks::installPlayerModel();
    hooks::installServer();
    hooks::installHud();
    hooks::installKeys();
    hooks::installGamepad();
    return true;
}

bool DoABarrelRoll::disable() {
    getSelf().getLogger().debug("Disabling...");
    hooks::uninstallGamepad();
    hooks::uninstallKeys();
    hooks::uninstallHud();
    hooks::uninstallServer();
    hooks::uninstallPlayerModel();
    hooks::uninstallPlayer();
    hooks::uninstallCamera();
    hooks::uninstallInput();
    return true;
}

} // namespace do_a_barrel_roll

LL_REGISTER_MOD(do_a_barrel_roll::DoABarrelRoll, do_a_barrel_roll::DoABarrelRoll::getInstance());
