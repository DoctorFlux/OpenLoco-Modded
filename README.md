# OpenLoco Modded

**26.09-modded.3 — the first consolidated modded release (edition 1).**

Build transport networks with automatic cargo and passenger transfers, shared lines, timetables, path/chain signals and visual block orders, using Locomotion's native windows, graphics and controls.

[Download Windows x64](https://github.com/DoctorFlux/OpenLoco-Modded/releases/tag/v26.09-modded.3) · [Release notes](docs/releases/v26.09-modded.3.md) · [Development status](docs/DEVELOPMENT_STATUS.md) · [Local build guide](README-LOCAL-SETUP.md)

This is an independent fork of [OpenLoco](https://github.com/OpenLoco/OpenLoco), the open-source reimplementation of **Chris Sawyer's Locomotion**. The original game's assets are required and are not included. The `.3` identifier is retained for build continuity; it is the first consolidated modded edition. The earlier modded.1 and modded.2 releases/tags are retired; their commits remain in repository history.

## What this modded edition adds

The comparison below uses original Locomotion and this fork's **OpenLoco 26.09 baseline, [`e71abfb3`](https://github.com/OpenLoco/OpenLoco/commit/e71abfb3)**. OpenLoco provides the engine and its existing improvements; the additions listed here are this fork's work on top of that baseline. It is not a claim to include every later upstream change.

| Area | Locomotion / upstream baseline | This modded edition |
| --- | --- | --- |
| Cargo and passengers | Native cargo quantities and classic loading | Destination-aware passengers, mail and freight, automatic multimodal transfers and linked feeder/final earnings |
| Routes | Vehicle order lists | Shared lines alongside private orders; edit one line for all assigned vehicles |
| Timetables | Ordinary orders and vehicle dispatch | Timetable targets, lateness information and automatic spacing |
| Railway signals | Native block signals | Additional path, chain and combined signals, with track occupation/reservation checks and required-waypoint protection |
| Orders | Stops, route-through orders, waypoints and cargo instructions | Optional visual If / Else if / Else, And/Or, Repeat/Forever, Break, Wait, Skip, Go to and Group blocks spanning multiple orders |
| Editing and route display | Native order windows and numbering | Compact inline Rules editing, drag/drop, current-program/next-order markers and conditional shared-line Path/All previews |
| Gameplay options | Existing game/OpenLoco options | Additional no-obsolescence, infinite-money and calendar-speed controls |

**Automatic transfers:** make normal routes such as coal mine → truck → combined road/rail station → train → power plant. Cargo selects a reachable final destination and changes vehicles at the interchange without Transfer/Unload orders. A normal station with no nearby industry can be an interchange. Passengers/mail can take bus or tram feeders at both ends of a rail, ship or air journey, including return journeys and several destinations sharing one station. Facilities sharing a station ID share cargo; separate stations need a connecting service.

Station and vehicle Cargo tabs show compact, expandable destination quantities. **Options → Miscellaneous → Cargo transfers** selects Automatic or Classic; automatic distribution is the default for new games and older saves, while explicit saved settings are retained. Feeder vehicles receive virtual transfer credit; company income is paid once on final delivery using the native payment formula.

**Shared lines and rules:** use ordinary routes, or click **Rules** in Vehicle Route / shared-line Orders to edit blocks in that same window. Stops and waypoints keep their base-game appearance. Vehicles on a shared line share the program while keeping independent waits and loop counters. Rules do not bypass physical movement or signal safety.

**Save compatibility:** packed SV5/SC5 files stay unchanged. Keep each save with its matching `.olines` v5, `.osignals` v3, `.orules` v1 and `.ocargodist` v1 companions. Missing CargoDist companions migrate native quantities automatically. Keep backups before moving saves between builds. The release does not add multiplayer, larger maps/object limits, NSF/NOF, coupling/shunting or a script/mod loader.

Fresh tagged Debug and Release builds succeed; both complete suites passed **566/566**, including **113 CargoDist checks**. Tagged-release verification is recorded in the [release notes](docs/releases/v26.09-modded.3.md). Only a Windows x64 package is published for this release; other platforms require source builds. Native game acceptance remains user-controlled.

---

![](https://user-images.githubusercontent.com/604665/55420349-1a2aea00-5577-11e9-87da-78fe5cdb09e1.png)

# Contents
- 1 - [Introduction](#1-introduction)
- 2 - [Downloading the game (pre-built)](#2-downloading-the-game-pre-built)
- 3 - [Contributing](#3-contributing)
- 4 - [Compiling the game](#4-compiling-the-game)
  - 4.1 - [Building prerequisites](#41-building-prerequisites)
  - 4.2 - [Compiling and running](#42-compiling-and-running)
- 5 - [Licence](#5-licence)
- 6 - [More information](#6-more-information)

---

### Build Status
|             | Windows / Linux / MacOS | Download |
|-------------|-----------------|----------|
| **master**  | ![CI](https://github.com/DoctorFlux/OpenLoco-Modded/workflows/CI/badge.svg) | [![GitHub release](https://img.shields.io/github/release/DoctorFlux/OpenLoco-Modded.svg)](https://github.com/DoctorFlux/OpenLoco-Modded/releases) |

### Chat

The upstream OpenLoco community has a Discord server for discussion of that project. Please report fork-specific issues in [this repository](https://github.com/DoctorFlux/OpenLoco-Modded/issues).

[![Discord](https://img.shields.io/discord/689445672390361176)](https://discord.gg/vEuNRHD)


# 1 Introduction

**OpenLoco** is an open-source re-implementation of *Chris Sawyer's Locomotion* (CSL), the spiritual successor to Transport Tycoon. OpenLoco aims to improve the game similar to how [OpenTTD](https://openttd.org) improved *Transport Tycoon*, and [OpenRCT2](https://openrct2.io) improved *RollerCoaster Tycoon*.

CSL was originally written in x86 assembly, building on top of the RollerCoaster Tycoon 2 engine. However, the engine has changed substantially enough that OpenLoco currently does not share its codebase with OpenRCT2.

Upstream reports its C++ reimplementation complete as of December 2025 and identifies multiplayer and increased limits as goals. This fork builds on that engine; it does not deliver multiplayer or a new save container. See the [upstream project](https://github.com/OpenLoco/OpenLoco) for its own current plans and releases.

---

# 2 Downloading the game (pre-built)

Download [26.09-modded.3 for Windows x64](https://github.com/DoctorFlux/OpenLoco-Modded/releases/tag/v26.09-modded.3), with its SHA-256 checksum. Extract the ZIP into a separate folder, run `OpenLoco.exe`, and select your Locomotion installation when prompted. This release supersedes the retired modded.1 and modded.2 downloads. Linux, macOS and BSD users can build from source; no verified binaries for those platforms accompany this release.

Please note that OpenLoco requires the asset files of the original Chris Sawyer's Locomotion to play the game.
It can be bought at e.g. [Steam](https://store.steampowered.com/app/356430/) or [GOG.com](https://www.gog.com/game/chris_sawyers_locomotion).

---

# 3 Contributing

We warmly welcome any contributions to the project, e.g. for C++ code (game implementation, bug fixes, features) or localisation (new translations).
Please have a look at our [issues for newcomers](https://github.com/OpenLoco/OpenLoco/issues?q=is%3Aopen+label%3A%22good+first+issue%22%2C%22hacktoberfest%22).

For code contributions, please stick to our [code style](https://github.com/OpenLoco/OpenLoco/wiki/Coding-Style).
You can use `clang-format` to apply these guidelines automatically.

---

# 4 Compiling the game

If you would like to contribute code to OpenLoco, please follow the instructions below to get started compiling the game.

If you just want to play this fork, [download its latest release](https://github.com/DoctorFlux/OpenLoco-Modded/releases/latest). The published modded.3 package is Windows x64. [Upstream OpenLoco releases](https://github.com/OpenLoco/OpenLoco/releases) have their own platform packages and feature set.

## 4.1 Building prerequisites

The following libraries/dependencies are used:
- [libpng](http://www.libpng.org/pub/png/libpng.html)
- [libzip](https://libzip.org)
- [OpenAL](https://www.openal.org/)
- [SDL3](https://github.com/libsdl-org/SDL/releases)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) (fetched during CMake generation)
- [fmt](https://github.com/fmtlib/fmt) (fetched during CMake generation)
- [sfl](https://github.com/slavenf/sfl-library) (fetched during CMake generation)
- [breakpad](https://github.com/google/breakpad) (only required on Windows)
- [tbb](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onetbb.html) (only required if using libstdc++)
- [gtest](https://github.com/google/googletest) (only required for building tests)

### Windows
- 10 / 11
- [Visual Studio 2022](https://www.visualstudio.com/vs/community/)
  - Desktop development with C++ (ensure MFC component is selected)
  - Dependencies are managed with [vcpkg](https://github.com/Microsoft/vcpkg)

### Linux
- cmake 3.22+
- make or ninja
- If building x86-32 then 32-bit versions of the libraries mentioned above
- Dependencies can optionally be managed by vcpkg

### MacOS
- cmake 3.22+
- make or ninja
- Libraries mentioned above installed to standard paths using e.g. Homebrew
- Dependencies can optionally be managed by vcpkg

---

## 4.2 Compiling and running

### Windows:

1. Check out the repository. This can be done using [GitHub Desktop](https://desktop.github.com) or [other tools](https://help.github.com/articles/which-remote-url-should-i-use).
2. With VS 2022 use the "Open a local folder" option to start the project file generation. This may take some time as it downloads dependencies.
3. After successful generation of the project files open "build/windows/openloco.sln".
4. Select a config Debug or Release and run Build -> Build Solution.
4. Run the game from "build/windows/<config>/OpenLoco.exe" or within VS.

Alternatively using CMake use the following commands.

1. Run `cmake --preset windows`
2. Run `cmake --build --preset windows-release`

### Linux:

The standard CMake build procedure is to install the required libraries, then:
```
cmake --preset posix
cmake --build --preset posix-release
```

Note: If building x86-32 installing some packages can be problematic. To work around this, you can use [our Docker images](https://github.com/OpenLoco/openloco-docker-build) for compilation.
    
Note: Due to issues with distro yaml-cpp packages, its source release is downloaded during CMake generation.

### MacOS

The standard CMake build procedure is to install the required libraries, then:
```
cmake --preset posix
cmake --build --preset posix-release
```

Note: Only arm64 builds have been tested.

---

# 5 Licence
**OpenLoco** is licensed under the MIT License.

---

# 6 More information
- [OpenLoco Modded releases](https://github.com/DoctorFlux/OpenLoco-Modded/releases)
- [Modded feature matrix](docs/FEATURE_MATRIX.md)
- [GitHub](https://github.com/OpenLoco/OpenLoco)
- [TT-Forums](https://www.tt-forums.net)
- [Locomotion subreddit](https://www.reddit.com/r/locomotion/)
