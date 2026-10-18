# Do a Barrel Roll (Bedrock)

A port of [Do a Barrel Roll](https://codeberg.org/enjarai/do-a-barrel-roll) by enjarai to Minecraft Bedrock Edition, as a [LeviLamina](https://github.com/LiteLDev/LeviLamina) client mod. It adds full pitch, yaw and roll to elytra flight.

## Compatibility

| Component                     | Version          |
| ----------------------------- | ---------------- |
| Minecraft Bedrock for Windows | 1.26.51.01       |
| LeviLamina                    | 26.51.6 (client) |

## Features

- Full pitch, yaw and roll while gliding, with smoothing, banking, automatic righting and momentum-based mouse.
- Camera roll and player model roll.
- Optional artificial horizon and momentum widget HUD.
- Optional thrust with smoke particles.
- Keyboard, mouse and controller support.

## Controls

Keys are listed in the controls screen under the mod's name.

| Action                                                     | Default |
| ---------------------------------------------------------- | ------- |
| Toggle mod                                                 | I       |
| Yaw left / right                                           | A / D   |
| Thrust forward                                             | W       |
| Toggle thrust, pitch up/down, roll left/right, thrust back | unbound |

Controller (XInput):

| Input      | Action                |
| ---------- | --------------------- |
| Right stick | Roll and pitch       |
| LB / RB    | Yaw left / right      |
| RT / LT    | Thrust forward / back |

## Configuration

The config is written on first start to `mods/DoABarrelRoll/config/config.json`, using the same layout as the Java mod. Main options:

| Option                                  | Default |
| --------------------------------------- | ------- |
| `general.mod_enabled`                   | `true`  |
| `general.controls.switch_roll_and_yaw`  | `false` |
| `general.controls.invert_pitch`         | `false` |
| `general.controls.momentum_based_mouse` | `false` |
| `general.controls.disable_when_submerged` | `true` |
| `general.hud.show_horizon`              | `false` |
| `general.banking.enable_banking`        | `true`  |
| `general.banking.banking_strength`      | `20.0`  |
| `general.banking.automatic_righting`    | `false` |
| `general.thrust.enable_thrust`          | `false` |
| `general.thrust.max_thrust`             | `2.0`   |
| `general.thrust.thrust_acceleration`    | `0.1`   |
| `general.thrust.thrust_particles`       | `true`  |
| `general.misc.enable_gamepad`           | `true`  |
| `general.misc.gamepad_deadzone`         | `0.15`  |
| `general.misc.thrust_allow_remote`      | `false` |
| `sensitivity.desktop` / `.controller`   | pitch 1.0, yaw 0.4, roll 1.0 |
| `sensitivity.camera_smoothing`          | pitch 1.0, yaw 2.5, roll 1.0 |

## Thrust limitation

Bedrock movement is server-authoritative. Thrust only works in singleplayer or in worlds you host, where the mod can make the in-process server accept the client's position. On remote servers thrust is disabled. Setting `general.misc.thrust_allow_remote` forces it on, which causes rubber-banding.

## Not ported

- Activation modes other than vanilla (triple jump, hybrid).
- In-game config screen (edit the JSON file).
- Other players seeing your roll.
- Server-synced config.

## Building

Requires [xmake](https://xmake.io). On Windows:

```
xmake f -y -p windows -a x64 -m release
xmake
```

The built mod is in `bin/DoABarrelRoll`.

## License

GPL-3.0, like the original mod. Credit for Do a Barrel Roll goes to [enjarai](https://codeberg.org/enjarai).
