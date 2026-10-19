# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0-mc26.51] - 2026-10-19

### Added

- Server variant for Bedrock Dedicated Server (LeviLamina server) with client/server networking: thrust on remote servers, other players see your roll, server-controlled config (`allowThrusting`, `forceEnabled`, `forceInstalled`, `maxThrustSpeed`).
- Activation modes (vanilla, triple jump, hybrid, hybrid toggle). Triple jump is untested against the server-side glide start.

### Fixed

- Third-person model orientation through loops.
- Thrust particles now also spawn at cruise speed.

## [0.1.0-mc26.51] - 2026-10-18

### Added

- First public Windows x64 client release for Minecraft Bedrock 1.26.51.01 and LeviLamina 26.51.6.
- Full pitch, yaw and roll elytra flight, with smoothing, banking and optional automatic righting.
- Camera roll in first and third person, and player model roll in third person.
- Horizon line and momentum-based mouse mode with a moving crosshair.
- Thrust with campfire smoke particles, in singleplayer and worlds you host.
- Keyboard keys (bindable in the controls screen) and Xbox controller support.
- On-screen messages when toggling the mod or thrust.
- A lip v3 client package manifest that keeps `config.json` across updates.
