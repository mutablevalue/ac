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

## Channels

The left and right mouse buttons are independent channels. Each has its own mode, timing, offset, enable state, and toggle binding, and either can run alone or both at once. Target application and the exit binding are shared.

## Modes

Each channel is independently either:

- **Normal:** continuously clicks that button while the channel is enabled.
- **Additive:** counts physical clicks on that button and generates only the deficit needed to reach the channel's target rate.
- **Hold:** clicks at the configured rate only while you physically hold that button, so holding left click gives you the full CPS instead of one click.

All three modes support CPS and millisecond timing. The optional offset varies every generated click around the configured rate; for example, 12 CPS with an offset of 2 produces 10–14 CPS timing. Mouse selection is automatic.

An Additive channel also takes an optional **additive start rate**. At `0` it always fills in. Above `0` it generates nothing until your physical clicking reaches that rate, and stops again as soon as you drop below it — so a light click never turns into a burst. Physical cadence is only tracked between 2 and 50 CPS, so a start rate below 2 has no effect.

FastClicker never generates clicks while its own window is focused. The optional application target can be selected from the GUI. FastClicker can be enabled from any window, but click output remains paused until the selected application is active.

The chooser lists applications from two sources and merges them. Running processes come from the session's own accounting: every application a desktop session launches lives in a systemd `app-*.scope` cgroup naming it, and Flatpak apps additionally identify themselves through `.flatpak-info`. The identity is resolved to a human name through its installed desktop entry, so `com.discordapp.Discord` is offered as *Discord*. X11/XWayland toplevels are then merged in, which adds anything the session did not launch as an application, such as a game started from a terminal. Nothing is hardcoded; installing a new application makes it selectable with no code change.

How a target is gated depends on what the compositor is willing to say:

- **The target has an X11/XWayland window.** Focus is tracked exactly, and output pauses the instant focus moves away.
- **The target is a Wayland-native client** (Discord and Roblox via Sober both are). GNOME's Mutter exposes neither `ext-foreign-toplevel-list-v1` nor `zwlr-foreign-toplevel-management-v1`, and `org.gnome.Shell.Introspect` is restricted to portal implementations, so no unprivileged process can learn which window holds focus — Mutter points `_NET_ACTIVE_WINDOW` at an internal proxy window carrying no `WM_CLASS` and no `_NET_WM_PID`. FastClicker detects that proxy and falls back to gating on the target being *running*, still refusing to click while its own window is focused. The GUI says so plainly instead of pretending focus is known. If a compositor ever does publish a foreign-toplevel protocol, exact gating can be restored without changing the picker.

### Hold mode and device capture

While a Hold channel is armed, FastClicker takes an exclusive `EVIOCGRAB` on the elected pointer and relays everything it does not capture — motion, scrolling, and every other button — through a `FastClicker Passthrough <device>` uinput clone of that device. The grab is required: while your physical mouse holds a button down, the compositor treats it as logically held, so synthetic release/press pairs from another device would not register as new clicks.

The capture is as narrow as possible:

- It engages only while a Hold channel is actually armed, and drops the instant the channel is disarmed, the daemon shuts down, a binding capture starts, or click output is not allowed — which includes the FastClicker window being focused, so you can always click your own GUI.
- It waits for the device to report no held buttons before engaging, so arming Hold mid-click can never strand a button.
- The keyboard is never grabbed, so the exit binding always works.
- The kernel releases the grab when the descriptor closes, so even `kill -9` restores the mouse immediately.

If another process already holds the pointer, the grab fails and the channel refuses to enable rather than double every click.

## Click multiplier

Each channel can turn one click into several. **Clicks per click** (1–10) makes every generated click emit that many complete press/release pairs, spaced by **gap between clicks** (5–150 ms) — the default 40 ms sits well inside the desktop double-click window, so a multiplier of 2 registers as a real double-click. The GUI shows the rate ceiling a given burst implies; a burst that would overrun its slot caps the rate rather than bursting past it.

**Multiply my own clicks too** extends this to your physical clicks: each real tap gets the remaining clicks appended after it, so a single tap registers as a double-click even with the autoclicker switched off. It is off by default, ignores presses held longer than 250 ms so drags are never multiplied, and obeys the same focus rules as generated output. In Additive mode each added click consumes a target slot, so the channel's configured CPS stays the combined total.

## Bindings

The default bindings are F8 to toggle the left channel, F9 to toggle the right channel, and Ctrl+Shift+F12 to exit. Click a binding in the GUI and press the key or mouse button you want; Esc cancels, and the prompt times out after ten seconds. Capture runs in the backend, so it reads real evdev codes and swallows the keypress instead of firing whatever it is currently bound to. Any key and any mouse button can be bound, including left and right click — the GUI warns when a binding is a mouse button, and a Hold channel may not be toggled by the button it captures. All three bindings must differ.

## Configuration compatibility

Configuration files written before per-button channels load unchanged: their single clicker becomes the left channel, and the right channel starts at its defaults. Files written before the click multiplier load with it switched off.

## Verifying device capture by hand

Unit tests cover the scheduler, bindings, configuration and protocol, but the grab needs `/dev/uinput` and a real mouse, so it is checked manually:

1. Run `udevadm monitor --property --subsystem-match=input` and arm a Hold channel. Expect one add event for the passthrough clone and no rescan storm.
2. `udevadm info /dev/input/eventN` on the clone must report `ID_INPUT_MOUSE=1`.
3. With the channel armed, confirm motion, scrolling and the other buttons still work, and that the click stream reaches the target application at the configured rate.
4. `libinput debug-events` should show buttons coming from `FastClicker Passthrough <device>` while the real node stays silent.
5. Toggle Hold on and off repeatedly; `ls /dev/input/event* | wc -l` must return to its original value.
6. Arm Hold *while* holding the button: the grab must wait for the release, leaving no stuck button.
7. Alt-tab to the FastClicker window with Hold armed: the grab must drop and the GUI stay clickable.
8. `kill -9` the daemon while grabbed: the mouse must work immediately.
9. With a 2× multiplier and "multiply my own clicks" on, a single tap must open a folder in a file manager, and dragging a window must leave no stray click at the drop point.
