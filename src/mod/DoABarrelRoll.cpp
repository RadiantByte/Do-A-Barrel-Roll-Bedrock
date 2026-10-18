#include "mod/DoABarrelRoll.h"

#include "ll/api/mod/RegisterHelper.h"

namespace do_a_barrel_roll {

DoABarrelRoll& DoABarrelRoll::getInstance() {
    static DoABarrelRoll instance;
    return instance;
}

bool DoABarrelRoll::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool DoABarrelRoll::enable() {
    getSelf().getLogger().debug("Enabling...");
    // Code for enabling the mod goes here.
    return true;
}

bool DoABarrelRoll::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    return true;
}

} // namespace do_a_barrel_roll

LL_REGISTER_MOD(do_a_barrel_roll::DoABarrelRoll, do_a_barrel_roll::DoABarrelRoll::getInstance());
