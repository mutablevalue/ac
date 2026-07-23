# FastClicker

Linux-only C++23 autoclicker using Dear ImGui, `evdev`, and `uinput`.

## First-time setup on Fedora

From this directory, run:

```bash
./scripts/setup-fedora.sh
```

The script installs the required packages, configures `/dev/uinput`, and builds the project. It avoids the fragile multiline package command that caused the `command not found` and missing RandR header errors.

If `/dev/uinput` remains inaccessible afterward, log out and back in once so the desktop-session ACL refreshes.

## Run

```bash
./run.sh
```

`run.sh` configures and rebuilds changed files before opening the GUI. The GUI launches its backend automatically, and closing the GUI stops and reaps that backend.

Run tests with:

```bash
./run.sh --test
```

## Project layout

```text
autoclicker/
├── src/
│   ├── Apps/       # GUI and daemon entry points
│   ├── Core/       # configuration, scheduling, lifecycle
│   ├── Gui/        # ImGui application
│   ├── Input/      # evdev, uinput, global hotkeys
│   ├── Ipc/        # Unix socket protocol
│   ├── Types/      # enums and passive data types only
│   ├── Utils/      # logging, errors, paths, file descriptors
│   └── Tests/      # deterministic unit tests
├── packaging/      # udev rule
├── scripts/        # environment setup
├── CMakeLists.txt
└── run.sh
```

The generated `.build/` directory is not source code. CMake places compiler output and downloaded dependency metadata there so generated files never mix with `src/`. It is ignored by Git and can be deleted safely whenever you want a clean rebuild.

## Modes

- **Normal:** continuously clicks while enabled.
- **Additive:** counts physical clicks and generates only the deficit needed to reach the configured target rate.

Both modes support CPS and millisecond timing. The optional offset varies every generated click around the configured rate; for example, 12 CPS with an offset of 2 produces 10–14 CPS timing. Mouse selection is automatic.

FastClicker never generates clicks while its own window is focused. The optional application target can be selected from the GUI. FastClicker can be enabled from any window, but click output remains paused until the selected application is focused and pauses immediately on focus loss. The chooser uses X11/XWayland window metadata available in Fedora GNOME. Native Wayland windows that do not publish XWayland metadata are not offered because their focus cannot be verified safely.

The default global bindings are F8 to toggle and Ctrl+Shift+F12 to exit. Both can be changed in the GUI.
