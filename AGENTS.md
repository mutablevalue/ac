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
- In Additive mode, configured CPS is the combined target: eligible physical clicks consume target slots and a continuous virtual-time deficit controller schedules only the missing slots. Never use a hard one-second quota to control output.
- Additive mode must establish a physical cadence before generating output. An isolated click must never create a later synthetic hit, physical release must never queue a click, and missed deadlines must never become a catch-up tail.
- Additive synthetic clicks are complete adjacent press/release pairs with a `SYN_REPORT` after each transition. Do not hold the virtual button across a physical-click opportunity.
- Automatic input discovery must examine every real `ID_INPUT_MOUSE=1` event node, then select exactly one authoritative `BTN_LEFT` source per `LIBINPUT_DEVICE_GROUP`, preferring a dedicated pointer over a hybrid keyboard/pointer interface. Always exclude the FastClicker virtual device.
- Deduplicate repeated button states at the scheduler boundary as a second safeguard so one physical action consumes exactly one target slot.
- Use absolute monotonic deadlines and deliberately drop missed ticks instead of producing catch-up bursts.
- Every stop path must cancel timers and release a pending synthetic button before closing descriptors.
- The uinput virtual mouse must advertise `BTN_LEFT`, `REL_X`, and `REL_Y`; Fedora/libinput must classify it with `ID_INPUT_MOUSE=1` or generated clicks will not reach desktop applications reliably.
- Treat shutdown as idempotent. GUI close, configured exit binding, IPC loss, `SIGINT`, `SIGTERM`, and parent death must converge on the same cleanup behavior.
- Never persist an enabled state or run the backend as root.

## Verification

- Add deterministic tests for scheduler and configuration changes.
- Add lifecycle coverage for any descriptor, process, timer, or virtual input ownership change.
- Unit tests must run without root, a display server, `/dev/uinput`, or a physical input device.
