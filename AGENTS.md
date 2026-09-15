# FastClicker engineering rules

These rules apply to the entire project and are part of its architecture, not optional style suggestions.

## Naming and language

- Use C++23 and the standard library before adding a dependency.
- Classes, types, enum values, constants, data members, parameters, and local variables use `PascalCase`.
- Free functions and methods use `snake_case`.
- Use `auto` when the initializer makes the type clear. Spell out types when they communicate domain meaning or prevent narrowing.
- Use named C++ casts (`static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast`) deliberately. Never use C-style casts.
- Represent time with `std::chrono` types, ownership with RAII, optionality with `std::optional`, and recoverable failure with `std::expected`.
- Prefer ranges algorithms to handwritten search/filter loops where they improve clarity.

## Architecture

- `src/Types` contains enums and passive data-only structures. Type headers must not perform I/O, own services, contain business logic, or depend on `Core`, `Input`, `Ipc`, `Gui`, or `Utils`.
- All C++ code, including applications and tests, belongs under the module directories directly inside `src`; do not add another project-name wrapper or top-level `include`, `apps`, or `tests` code trees.
- `Core`, `Input`, `Ipc`, `Gui`, and `Utils` contain behavior. Do not declare shared enums or passive transport/configuration structures inside logic modules.
- Keep one behavior class per header/source pair. Headers expose the smallest usable interface and source files hold implementation logic.
- Application `main.cpp` files only compose objects and translate the final result into an exit code.
- Dependencies flow from behavior modules toward `Types`; `Types` never depends back on behavior.
- Keep platform code inside `Input`, process/socket transport inside `Ipc`, presentation inside `Gui`, and general reusable infrastructure inside `Utils`.

## Performance and safety

- Never allocate, format strings, parse configuration, or write logs in the click scheduling path.
- The left and right mouse buttons are independent channels. Each owns one `ClickScheduler` with its own configuration, enable state, cadence tracking and toggle binding, and neither channel's state may leak into the other. Shared configuration is limited to the target application and the exit binding.
- In Additive mode, a channel's configured CPS is its combined target: eligible physical clicks on that button consume target slots and a continuous virtual-time deficit controller schedules only the missing slots. Never use a hard one-second quota to control output.
- Additive mode must establish a physical cadence before generating output. An isolated click must never create a later synthetic hit, physical release must never queue a click, and missed deadlines must never become a catch-up tail.
- A non-zero additive start rate suppresses generated output until the smoothed physical cadence reaches it, and resumes suppression the moment it drops below. Evaluate the gate from the smoothed interval rather than a sampled window, and hold credit at a single slot while suppressed so opening the gate can never release a burst.
- Additive synthetic clicks are complete adjacent press/release pairs with a `SYN_REPORT` after each transition. Do not hold the virtual button across a physical-click opportunity.
- Hold mode is the only mode permitted to emit while its own button is physically down, because it captures that button. Every other mode must keep refusing to press through a held physical button.
- Hold mode requires an exclusive device grab. Engage only while a Hold channel is armed and only when the device reports no held keys, release on disarm, shutdown, binding capture, and any loss of output permission, and never grab the keyboard so the exit binding always works. Relay every uncaptured event to the passthrough clone verbatim, including the source's own `EV_SYN` frames; never synthesize a `SYN_REPORT` per event.
- Uinput devices this project creates must be identifiable by name so discovery excludes them and their own hotplug events never trigger a rescan. Creating a device that a rescan would tear down is a feedback loop, not a cosmetic issue.
- The click multiplier's generated burst and the physical-click multiplier are separate timelines. Physical multiplication runs even while the channel is disabled, is gated on output permission rather than enable state, must ignore presses long enough to be drags, and must consume an additive slot per added click so the configured CPS stays the combined target.
- Keep every multiplier behaviour gated on a multiplier greater than one so the single-click path stays byte-identical and the existing scheduler tests remain its regression proof.
- Binding capture belongs to the backend, which is the only side that sees evdev codes. An armed capture must suppress hotkey dispatch and scheduler input entirely, and must expire on its own so a lost client cannot strand it.
- Automatic input discovery must examine every real `ID_INPUT_MOUSE=1` event node, then select exactly one authoritative `BTN_LEFT` source per `LIBINPUT_DEVICE_GROUP`, preferring a dedicated pointer over a hybrid keyboard/pointer interface. Always exclude the FastClicker virtual device. Right-button events are read from that same elected node so both channels observe one source.
- Deduplicate repeated button states at the scheduler boundary as a second safeguard so one physical action consumes exactly one target slot.
- Use absolute monotonic deadlines and deliberately drop missed ticks instead of producing catch-up bursts.
- Every stop path must cancel timers and release a pending synthetic button before closing descriptors.
- The uinput virtual mouse must advertise `BTN_LEFT`, `BTN_RIGHT`, `REL_X`, and `REL_Y`; Fedora/libinput must classify it with `ID_INPUT_MOUSE=1` or generated clicks will not reach desktop applications reliably.
- Application discovery must never contain a list of known applications. Identity comes from the session's own accounting — the systemd `app-*.scope` cgroup, `.flatpak-info`, and installed desktop entries — so a newly installed application is selectable with no code change. An X11 window class is a fallback identity, never the primary one, because it is a toolkit detail and invisible for Wayland-native clients.
- Distinguish focus that is unsupported from focus that is unobservable. When a Wayland-native client holds focus, the compositor names nothing, and reporting that as "no application focused" would silently block every click. Report the degraded state, gate the target on being running, and keep refusing to click through the FastClicker window. Never treat an empty active-window name as a match.
- Treat shutdown as idempotent. GUI close, configured exit binding, IPC loss, `SIGINT`, `SIGTERM`, and parent death must converge on the same cleanup behavior.
- Never persist an enabled state or run the backend as root.

## Verification

- Add deterministic tests for scheduler and configuration changes.
- Add lifecycle coverage for any descriptor, process, timer, or virtual input ownership change.
- Unit tests must run without root, a display server, `/dev/uinput`, or a physical input device.
