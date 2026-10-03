# Hyprland Windows

An experimental Windows adaptation of [Hyprland](https://github.com/hyprwm/Hyprland), bringing its dynamic tiling workflow and visual design to native Windows applications.

This fork is maintained independently by [arizkami](https://github.com/arizkami). It is not an official Hyprland project and is not affiliated with or endorsed by the upstream maintainers.

> **Status: early development.** The repository currently contains the inherited Hyprland codebase. The native Windows backend and GraphicEngineCore described below are planned work, not implemented features. There is no supported Windows build or installation procedure yet.

## Direction

Hyprland Windows aims to combine dynamic window management with a native, GPU-rendered desktop interface.

The Windows adaptation will require substantial rewrites of the platform-dependent code. Layout algorithms, window rules, workspace policies, keybinding behavior, and animation mathematics will be adapted where practical. Wayland surfaces, Linux input handling, display output, and platform integration will be replaced with Windows implementations.

On Windows, the Desktop Window Manager (DWM) remains responsible for desktop composition. This project will manage native application windows through Win32 APIs and render its own interface through Direct3D and DirectComposition. It does not replace DWM or provide a Wayland session for Windows applications.

## Planned architecture

### GraphicEngineCore

GraphicEngineCore is the planned graphics and UI foundation:

**DXGI + DirectComposition + Direct3D 11 + HLSL + DirectWrite + Yoga**

| Component | Responsibility |
| --- | --- |
| Direct3D 11 | GPU resources, textures, buffers, and rendering passes |
| HLSL | Custom drawing, rounded shapes, gradients, shadows, blur, and other shader effects |
| DXGI | Graphics adapters, swap chains, and presentation |
| DirectComposition | Composition of the project's visual layers, transforms, clipping, opacity, and animations |
| DirectWrite | Text shaping, layout, font fallback, and glyph metrics |
| Yoga | Flexbox layout for the project's UI widgets |
| Win32 | Native windows, message loop, input, DPI, and platform lifecycle |

The graphics engine will keep UI layout, drawing commands, and composition layers separate. Text rasterization and caching will be integrated with the rendering backend.

Yoga will arrange UI widgets such as panels, launchers, and settings. Dynamic tiling layouts will use their own layout algorithms rather than being represented as Flexbox layouts.

### Window management

A separate window-management core will track native `HWND` windows and maintain layout, workspace, rule, and focus state.

Planned Windows integration includes:

- Window discovery and lifecycle tracking through `EnumWindows` and `SetWinEventHook`.
- Window placement through `SetWindowPos` and deferred positioning APIs.
- Global shortcuts and input handling through Win32 hotkeys, Raw Input, and hooks where needed.
- Per-monitor DPI handling and work-area awareness.
- Windows-native configuration watching and IPC.
- Project-owned borders, panels, popups, and workspace UI rendered by GraphicEngineCore.

The core should operate on window IDs, geometry, constraints, and commands so that layout policy remains separate from Win32 calls.

## Roadmap

All items below are planned; this list does not describe currently working Windows functionality.

1. **Native foundation:** establish the Windows build, application lifecycle, and GraphicEngineCore rendering prototype.
2. **Window management:** discover and track native windows; implement Dwindle and Master tiling, gaps, floating windows, and basic shortcuts.
3. **Application compatibility:** handle dialogs, owned windows, size constraints, fullscreen behavior, multiple monitors, and mixed DPI.
4. **Configuration and control:** adapt window rules, configuration reload, command dispatch, and IPC.
5. **Desktop interface:** add custom borders, panels, a launcher, and workspace controls.
6. **Workspaces and effects:** develop workspace visibility policies, previews, and transitions; investigate capture-based effects where appropriate.
7. **Reliability:** restore managed window state on exit, recover graphics resources after device loss, and expand compatibility testing.

## Windows boundaries

Native Windows applications retain their own rendering and window behavior. Managing their position is different from composing their surfaces directly.

- DirectComposition can animate the project's own visuals; it does not automatically grant control over every application's composition tree.
- Moving or resizing a live application window may trigger application redraws and is not equivalent to transforming a texture.
- Captured window previews may support overview and transition effects, but they introduce capture, visibility, latency, and input constraints.
- Focus changes are subject to Windows foreground-activation rules.
- Hyprland-style workspaces require a Windows-specific implementation. Full compatibility with Windows virtual desktops is not assumed.
- Linux/Wayland plugins, configuration actions, and integrations are not automatically compatible with this fork.

## Building

A supported native Windows build is not available yet.

The existing CMake files, dependencies, scripts, and Linux/Wayland documentation are inherited from upstream. They should not be treated as Windows build instructions. Windows prerequisites and build steps will be documented when the first native backend is available.

## Contributing

Windows adaptation work belongs in [arizkami/Hyprland-Windows](https://github.com/arizkami/Hyprland-Windows). Target this fork for Windows-specific pull requests and development; do not submit this fork's porting work to the upstream Hyprland repository.

Contributions should clearly distinguish planned behavior from implemented and tested functionality. Preserve upstream attribution and applicable license notices when adapting code.

## Credits and license

This project builds on [Hyprland](https://github.com/hyprwm/Hyprland), created by vaxerski and its contributors. Their work provides the foundation for this adaptation.

The inherited Hyprland code is licensed under the **BSD 3-Clause License**. See [LICENSE](LICENSE). Retain the applicable copyright, license conditions, and disclaimer when redistributing adapted source or binaries.

Third-party components retain their own licenses and notices.
