# Hyprland Windows

An independent, experimental Windows adaptation of [Hyprland](https://github.com/hyprwm/Hyprland), maintained by [arizkami](https://github.com/arizkami). It is not an official Hyprland project and is not endorsed by upstream.

**The first native milestone is implemented in `windows/`.** It manages existing top-level HWND windows and renders its own control panel. Windows DWM remains the desktop compositor. No DLL injection, window reparenting, Explorer replacement, or access to arbitrary application composition surfaces is involved.

## Implemented milestone

- Isolated C++20/MSVC x64 CMake build, without the inherited Linux dependency stack.
- Win32 panel with Per-Monitor V2 DPI awareness, resize handling, pointer controls and Tab/Enter/Space keyboard navigation.
- GraphicEngineCore: D3D11, HLSL rounded rectangles/borders/gradients, a premultiplied-alpha DXGI composition swap chain, DirectComposition, DirectWrite system font fallback, and Yoga UI layout.
- Initial EnumWindows discovery plus out-of-context WinEvent hooks for lifecycle, visibility, foreground, minimization, geometry and interactive movement.
- Window records with monotonically assigned internal IDs, HWND, PID/thread, available executable path, title/class, owner, visible frame bounds, monitor, minimized state and floating policy. Destroy events remove records; PID/thread/class changes invalidate stale HWND records.
- Dwindle (successive longer-axis splits) and Master (60% master plus vertically divided stack) in a platform-independent geometry core. Initial ordering follows discovery; subsequent windows append by internal ID. This is a minimal adaptation, not the full upstream configurable layout implementation.
- Monitor work areas in physical screen coordinates, with DIP gaps scaled per monitor. Both inner and outer gaps use the panel's gap value (0–64 DIP); the geometry API accepts independent values.
- Owned dialogs and fixed-size windows float by default. Own-process, shell, hidden, cloaked, tool and child windows are excluded.
- Directional focus, floating toggle, opt-in management, hotkeys, and restoration on disable or normal exit.
- WM_GETMINMAXINFO negotiation with a bounded timeout, DWM invisible-frame compensation, SetWindowPos without activation/z-order changes, and delayed reconciliation of actual visible bounds. Windows that cannot fit or reject placement are restored and left floating; remaining windows relayout. An explicit user drag opts that window out until toggled back.
- Event-driven rendering and coalesced, one-shot relayout/reconciliation timers. No desktop polling loop or continuous idle redraw.
- RAII resources, graphics resource recreation on device removal/reset or D2D target loss, and a session-local guard against duplicate managers.

## Windows prerequisites and build

Use Windows 10/11 x64 with an interactive desktop and DWM enabled. Initial support targets MSVC (Visual Studio 2022 or later), CMake 3.24+, Git, and a Windows 10/11 SDK. In Visual Studio Installer, install **Desktop development with C++**, including the x64 MSVC compiler and Windows SDK. CMake must be on PATH. A first configuration needs network access to fetch Yoga.

From the repository root in PowerShell, using Visual Studio 2022:

```powershell
cmake -S windows -B windows/build -G "Visual Studio 17 2022" -A x64
cmake --build windows/build --config Release --parallel 4
.\windows\build\Release\HyprlandWindows.exe
```

For Visual Studio 2026, use `-G "Visual Studio 18 2026"` instead. The verified build used MSVC 19.50 and Windows SDK 10.0.26100.0. Select one generator per build directory; use another directory when switching versions.

Yoga 3.2.1 is pinned to commit `042f5013152eb81c1552dec945b88f7b95ca350f`. Only `yogacore` is built: its upstream test dependencies are not fetched. There are no other new third-party runtime libraries. To build offline after obtaining the matching source, pass `-DFETCHCONTENT_SOURCE_DIR_YOGA=C:/path/to/yoga` at configure time. CMake downloads dependencies into the ignored build directory.

The inherited root `CMakeLists.txt`, `src/`, Linux build instructions and Linux tooling remain unchanged. `cmake -S .` still selects that inherited build and is not a Windows build command.

## Controls and restoration

Launching opens the panel with **management disabled**. Click **Enable desktop tiling** or press Ctrl+Alt+T to opt in. This enables tiling of eligible existing windows on all monitors. The panel remains a normal movable/resizable window and is never tiled by the manager.

| Shortcut | Action |
| --- | --- |
| Ctrl+Alt+T | Enable, or disable and restore |
| Ctrl+Alt+L | Switch Dwindle/Master |
| Ctrl+Alt+F | Toggle the foreground application's floating state |
| Ctrl+Alt+Left/Right/Up/Down | Request directional foreground focus |
| Ctrl+Alt+Q | Restore windows and exit |
| Tab, then Enter or Space | Navigate/activate panel controls |

Panel buttons also change the gaps and restore/exit. Hotkey registration conflicts are reported in the panel; available controls still work. Registrations are released on exit. Focus uses SetForegroundWindow and respects Windows activation restrictions; declined requests are reported rather than bypassed.

Original WINDOWPLACEMENT is saved immediately before first managing a window, including its normal rectangle and maximized state. Disable, floating toggle, rejected placement and normal exit restore surviving windows. Hidden windows remain hidden during restoration. A user drag preserves the user's new geometry while management runs; its saved placement is still restored on disable/exit. Failed restorations are reported and their snapshots retained for a subsequent attempt. Snapshots are in memory: forced termination, a process crash, application refusal, or a hung/elevated application can prevent restoration. Closing a surviving application is never part of restoration.

## Architecture

| Layer | Files | Responsibility |
| --- | --- | --- |
| Portable policy | `windows/src/Layout.*` | Geometry, Dwindle/Master math, filtering policy and directional selection; no Win32/Yoga |
| Win32 integration | `windows/src/WindowManager.*` | Discovery, identity, hooks, metadata, work areas, constraints, placement and restoration |
| UI layout | `windows/src/Panel.*` | Yoga Flexbox layout, drawing commands, rounded/clipped hit regions, control state |
| GraphicEngineCore | `windows/src/Graphics.*` | D3D11/HLSL shapes, D2D GPU text rasterization, DirectWrite fallback, DXGI presentation and composition resources |
| Application | `windows/src/Main.cpp` | Window/message lifecycle, DPI, inputs, hotkeys and one-shot scheduling |

The panel's background, controls and text share one swap chain and one composition visual. Layout nodes, drawing commands and the composition layer are separate. DirectComposition only composes this application's visual. Direct2D rasterizes DirectWrite layouts directly onto the DXGI surface, using grayscale text antialiasing on a premultiplied-alpha target. HLSL emits premultiplied colors and uses matching source-over blending.

The UI thread owns all D3D11 immediate-context use, D2D operations and management state. Hook callbacks only post events to the Win32 queue. Structural changes trigger a debounced relayout; duplicate geometry feedback does not repeat unchanged placement requests. A one-shot reconciliation handles delayed application/DWM updates without an idle timer. ABI pointers are confined to Win32/COM/Yoga interfaces; ownership uses WRL ComPtr, standard C++ RAII and a Yoga tree wrapper, avoiding a dependency on the Linux-oriented Hyprutils build.

The layout mathematics were adapted after inspecting upstream `src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.cpp` and `master/MasterAlgorithm.cpp`. The original compositor globals, Wayland targets and Linux services are not linked into the Windows application. Upstream attribution and the BSD license are retained.

## Tests and observed validation

Close any running Hyprland Windows panel before the native test. On an interactive Windows desktop:

```powershell
ctest --test-dir windows/build -C Release --output-on-failure
```

Three suites cover geometry/filter policy, Yoga layout and rounded/clip hit testing, and a native integration harness. The harness creates its own test windows and launches the manager with `--test-process <fixture PID>`, restricting discovery/hooks/placement to that process. It exercises actual hotkeys using SendInput, changes foreground focus, and closes its fixtures afterward. It never enables management of the user's other applications. The scoped mode also exposes local WM_APP test controls/telemetry; normal launches ignore those messages. It must not be used as a production IPC API.

For a headless Windows build, run only the unit suites:

```powershell
ctest --test-dir windows/build -C Release -R "layout_policy|panel_layout_clipping" --output-on-failure
```

The geometry/policy suite can also configure on other platforms without Yoga or Windows dependencies:

```sh
cmake -S windows -B windows/build-portable
cmake --build windows/build-portable
ctest --test-dir windows/build-portable --output-on-failure
```

Validation on 2026-10-04:

- MSVC Release build and all three suites passed on Windows 11.
- Native fixtures verified opt-in startup, both layouts, actual registered hotkeys and directional focus, owned-dialog/tool filtering, appearance/minimize/restore/destruction, hidden-window preservation, constraint negotiation and delayed rejected-geometry fallback, pointer disable, resize presentation, original/maximized-placement restoration and hotkey cleanup.
- The test observed one monitor at 144 DPI (150%). Initial panel DIP sizing and monitor placement passed. Mixed-DPI transitions across multiple physical monitors remain unverified.
- Native idle check: no presented frames and 0 ms reported process CPU time over 1.1 seconds. This is a short observation, not a long-duration performance benchmark.
- The actual rendered panel was visually inspected for rounded shapes/gradient/border, multilingual Thai/Japanese/Arabic text fallback, panel layout and pointer selection. An initial high-DPI clipping issue was found and fixed.
- Real GPU device removal/reset recovery, Windows 10 execution, long-duration desktop operation, diverse third-party application compatibility, elevated/hung applications and deterministic same-process HWND reuse are not runtime-verified. Device-loss recovery paths exist but should not be described as hardware-reset tested.

## Limitations and remaining roadmap

This is a reversible native prototype, not a production window manager. Panel settings are session-only. There are no inherited configuration-option changes or promises of Linux config/plugin compatibility. Resizable windows are considered candidates, so specialized applications may need manual floating opt-out. Fullscreen applications, application-specific window rules, advanced minimum-size allocation and comprehensive shell-class compatibility need further work. Work-area overcapacity leaves windows at their original placements; constrained windows float rather than forcing an invalid layout.

Next work includes broader app compatibility, mixed-DPI multi-monitor validation, configuration and window rules, and stronger crash recovery. Workspace switching, capture-based animations, arbitrary application blur, plugin compatibility, Explorer replacement and advanced desktop effects are deferred. DirectComposition does not expose arbitrary application surfaces, and this project does not replace DWM.

## Credits and licenses

Hyprland was created by vaxerski and its contributors. This independent adaptation retains the inherited **BSD-3-Clause** [LICENSE](LICENSE), copyright conditions and disclaimer. New Windows adaptation code follows that repository license. Yoga retains its **MIT** notice in [windows/third-party/Yoga-LICENSE.txt](windows/third-party/Yoga-LICENSE.txt); include it when distributing binaries linked with Yoga. Other inherited third-party notices remain unchanged.

Windows work belongs in [arizkami/Hyprland-Windows](https://github.com/arizkami/Hyprland-Windows). Upstream contributions and developer-facing communication remain subject to Hypr's [AI usage policy](https://github.com/hyprwm/.github/blob/main/policies/AI_USAGE.md) and [issue guidelines](https://wiki.hypr.land/contributing-and-debugging/issue-guidelines/). AI agents must not interact with upstream PRs/issues/discussions; violations can lead to permanent bans. This milestone was implemented and tested with AI assistance and must be reviewed by its human maintainer.
