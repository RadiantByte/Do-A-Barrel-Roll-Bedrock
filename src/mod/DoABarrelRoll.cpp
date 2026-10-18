#include "mod/DoABarrelRoll.h"

#include "ll/api/mod/RegisterHelper.h"
#include "mod/hooks/State.h"

namespace do_a_barrel_roll {

DoABarrelRoll& DoABarrelRoll::getInstance() {
    static DoABarrelRoll instance;
    return instance;
}

bool DoABarrelRoll::load() {
    auto& logger      = getSelf().getLogger();
    state::configPath = getSelf().getConfigDir() / "config.json";
    if (std::filesystem::exists(state::configPath)) {
        if (!state::config.load(state::configPath)) {
            logger.warn("Could not parse {}, using defaults", state::configPath.string());
        }
    } else if (!state::config.save(state::configPath)) {
        logger.warn("Could not write default config to {}", state::configPath.string());
    }
    return true;
}

bool DoABarrelRoll::enable() {
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
