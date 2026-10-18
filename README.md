# Do a Barrel Roll (Bedrock)

A port of [Do a Barrel Roll](https://codeberg.org/enjarai/do-a-barrel-roll) to Minecraft Bedrock Edition, as a [LeviLamina](https://github.com/LiteLDev/LeviLamina) client mod: full pitch, yaw and roll for elytra flight.

**Status: builds, but completely untested in game.** Several unit/sign assumptions need calibration (see below).

## Features

- Full pitch/yaw/roll while gliding: mouse look is intercepted and fed through a port of the Java flight controller (smoothing, banking, auto-righting, momentum mouse, sensitivity).
- Camera roll (first and, if it goes through the same camera setup, third person).
- Optional artificial horizon and momentum crosshair HUD.
- Optional thrust.

Not ported: TRIPLE_JUMP / HYBRID activation (behaves like VANILLA), thrust smoke particles, player model roll, server-side config sync, config screen.

## Keybinds

Registered in the controls screen under the mod (key codes are Windows VK codes). Defaults follow the Java mod:

| Action | Default |
| --- | --- |
| Toggle mod | I |
| Yaw left / right | A / D |
| Thrust forward | W |
| Toggle thrust, pitch up/down, roll left/right, thrust backward | unbound |

## Config

`config.json` in the mod's config directory, same layout as the Java mod's client config. Extra Bedrock calibration options under `general.misc`: `debug_logging` (logs the first 40 turn deltas), `turn_delta_scale`, `swap_turn_axes`, `camera_roll_sign`.

## Compatibility

| Component | Version |
| --- | --- |
| Minecraft Bedrock for Windows | 1.26.51.01 |
| LeviLamina | 26.51.6, client variant |

## Building

```
xmake f -y -p windows -a x64 -m release
xmake
```

The built mod is in `bin/DoABarrelRoll`.

## License

GPL-3.0, like the original mod.
