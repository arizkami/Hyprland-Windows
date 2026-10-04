# Upstream Hyprland sources (src/) compiled for Windows against the compat layer in
# windows/compat. HYPRLAND_PORTED_SOURCES lists the files that build today; each
# porting stage grows it until the compositor core is complete.
include(FetchContent)

cmake_path(SET HYPRLAND_PLATFORM_HEADER NORMALIZE "${CMAKE_CURRENT_LIST_DIR}/../compat/Platform.hpp")

# libwayland-server with the Windows backend from windows/patches/wayland:
# a Win32 event loop and a display that accepts no clients (no Unix-socket
# transport). wayland-util.c is used unchanged.
hypr_fetch(wayland https://gitlab.freedesktop.org/wayland/wayland.git 736d12ac67c20c60dc406dc49bb06be878501f86) # 1.24.0
set(WAYLAND_VERSION_MAJOR 1)
set(WAYLAND_VERSION_MINOR 24)
set(WAYLAND_VERSION_MICRO 0)
set(WAYLAND_VERSION 1.24.0)
configure_file("${wayland_SOURCE_DIR}/src/wayland-version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/wayland/wayland-version.h" @ONLY)
add_library(wayland_server STATIC
    "${wayland_SOURCE_DIR}/src/event-loop-win32.c"
    "${wayland_SOURCE_DIR}/src/wayland-server-win32.c"
    "${wayland_SOURCE_DIR}/src/wayland-util.c")
set_target_properties(wayland_server PROPERTIES C_STANDARD 11)
target_include_directories(wayland_server PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../compat/include"
    "${wayland_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/wayland")
target_compile_options(wayland_server PRIVATE /W3 /wd4244 /wd4267 /utf-8 "/FI${HYPRLAND_PLATFORM_HEADER}")

# xkbcommon: headers only for now. Keymap code uses its declarations; the library
# arrives with keyboard input.
FetchContent_Declare(xkbcommon
    GIT_REPOSITORY https://github.com/xkbcommon/libxkbcommon.git
    GIT_TAG xkbcommon-1.11.0
    GIT_SHALLOW TRUE
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR windows-no-cmake)
FetchContent_MakeAvailable(xkbcommon)

# libinput: header only. Input configuration code calls libinput on device handles,
# which are always null on Windows (the Win32 backend has no libinput devices).
FetchContent_Declare(libinput
    GIT_REPOSITORY https://gitlab.freedesktop.org/libinput/libinput.git
    GIT_TAG 1.29.0
    GIT_SHALLOW TRUE
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR windows-no-cmake)
FetchContent_MakeAvailable(libinput)
add_library(libinput_headers INTERFACE)
target_include_directories(libinput_headers INTERFACE "${libinput_SOURCE_DIR}/src")
add_library(xkbcommon_headers INTERFACE)
target_include_directories(xkbcommon_headers INTERFACE "${xkbcommon_SOURCE_DIR}/include")

find_package(unofficial-lua CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(muparser CONFIG REQUIRED)

set(HYPRLAND_PORTED_SOURCES
    bell/BellPlayer.cpp
    bell/impl/RotatingPlayerThread.cpp
    config/ConfigValue.cpp
    config/lua/bindings/Check.cpp
    config/lua/types/LuaConfigBool.cpp
    config/lua/types/LuaConfigColor.cpp
    config/lua/types/LuaConfigCssGap.cpp
    config/lua/types/LuaConfigExpressionVec2.cpp
    config/lua/types/LuaConfigFloat.cpp
    config/lua/types/LuaConfigFontWeight.cpp
    config/lua/types/LuaConfigGradient.cpp
    config/lua/types/LuaConfigInt.cpp
    config/lua/types/LuaConfigString.cpp
    config/lua/types/LuaConfigUtils.cpp
    config/lua/types/LuaConfigValue.cpp
    config/shared/animation/AnimationTree.cpp
    config/shared/workspace/WorkspaceRule.cpp
    config/values/ConfigValues.cpp
    config/values/types/BoolValue.cpp
    config/values/types/ColorValue.cpp
    config/values/types/CssGapValue.cpp
    config/values/types/FloatValue.cpp
    config/values/types/FontWeightValue.cpp
    config/values/types/GradientValue.cpp
    config/values/types/IValue.cpp
    config/values/types/IntValue.cpp
    config/values/types/StringValue.cpp
    config/values/types/Vec2Value.cpp
    desktop/reserved/ReservedArea.cpp
    desktop/rule/Rule.cpp
    desktop/rule/layerRule/LayerRuleEffectContainer.cpp
    desktop/rule/matchEngine/BoolMatchEngine.cpp
    desktop/rule/matchEngine/IntMatchEngine.cpp
    desktop/rule/matchEngine/MatchEngine.cpp
    desktop/rule/matchEngine/RegexMatchEngine.cpp
    desktop/rule/matchEngine/TagMatchEngine.cpp
    desktop/rule/matchEngine/WorkspaceMatchEngine.cpp
    desktop/rule/windowRule/WindowRuleEffectContainer.cpp
    desktop/state/Fadeout.cpp
    desktop/state/ViewState.cpp
    desktop/state/ViewStateTracker.cpp
    desktop/view/focusable/Focusable.cpp
    desktop/view/popup/PopupBackend.cpp
    desktop/view/surfaceTree/SubsurfaceOwner.cpp
    desktop/view/types/AlphaModifiable.cpp
    desktop/view/types/Geometric.cpp
    desktop/view/types/GeometricAnimated.cpp
    desktop/view/types/GeometricMovableAnimated.cpp
    desktop/view/types/GeometricMovableStatic.cpp
    desktop/view/types/GeometricStatic.cpp
    desktop/view/window/WindowBackend.cpp
    desktop/view/window/WindowFullscreenPolicy.cpp
    desktop/view/window/WindowMetadata.cpp
    devices/IHID.cpp
    devices/IPointer.cpp
    devices/ITouch.cpp
    helpers/CMType.cpp
    helpers/DeformableMesh.cpp
    helpers/MotionBlur.cpp
    helpers/TagKeeper.cpp
    helpers/env/Env.cpp
    helpers/fs/FsUtils.cpp
    helpers/math/Direction.cpp
    helpers/math/Expression.cpp
    helpers/math/Math.cpp
    helpers/time/Timer.cpp
    i18n/Engine.cpp
    layout/algorithm/tiled/GeometryMath.cpp
    managers/DonationNagManager.cpp
    managers/eventLoop/EventLoopTimer.cpp
    managers/input/trackpad/gestures/ITrackpadGesture.cpp
    output/DamageRing.cpp
    pointer/PointerTransformer.cpp
    pointer/cursor/CursorShapeOverrideController.cpp
    protocols/types/DataDevice.cpp
    render/SyncFDManager.cpp
    render/decorations/AnimatedDecorationGradient.cpp
    render/decorations/IHyprWindowDecoration.cpp
    render/pass/BorderPassElement.cpp
    render/pass/ClearPassElement.cpp
    render/pass/FramebufferElement.cpp
    render/pass/InnerGlowPassElement.cpp
    render/pass/PassElement.cpp
    render/pass/PreBlurElement.cpp
    render/pass/ShadowPassElement.cpp
    render/pass/TransformedWindowPassElement.cpp
    state/MonitorQueryCore.cpp
    state/workspace/LifecyclePolicy.cpp
    state/workspace/Query.cpp
    state/workspace/State.cpp
    state/workspace/StateTracker.cpp
    workspace/AbstractWorkspace.cpp
    workspace/RegularWorkspace.cpp
    workspace/SpecialWorkspace.cpp
    workspace/filter/statement/DisplayNamePrefixStatement.cpp
    workspace/filter/statement/DisplayNameSuffixStatement.cpp
    workspace/filter/statement/FullscreenStatement.cpp
    workspace/filter/statement/IDStatement.cpp
    workspace/filter/statement/MonitorStatement.cpp
    workspace/filter/statement/NamedAddressableNameStatement.cpp
    workspace/filter/statement/NamedStatement.cpp
    workspace/filter/statement/RangeStatement.cpp
    workspace/filter/statement/SpecialAddressableNameStatement.cpp
    workspace/filter/statement/SpecialStatement.cpp
    workspace/filter/statement/WindowCountStatement.cpp
    workspace/query/Query.cpp
)

set(HYPRLAND_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
cmake_path(NORMAL_PATH HYPRLAND_SOURCE_DIR)
list(TRANSFORM HYPRLAND_PORTED_SOURCES PREPEND "${HYPRLAND_SOURCE_DIR}/src/")

# src/version.h, generated into the source tree (gitignored) exactly as the Linux
# build does. GIT_* values come from windows/CMakeLists.txt, already C-escaped.
hypr_version(hyprutils HYPRUTILS_VERSION)
hypr_version(hyprlang HYPRLANG_VERSION)
hypr_version(hyprgraphics HYPRGRAPHICS_VERSION)
set(HYPRCURSOR_VERSION "unavailable")
string(REPLACE "." ";" AQUAMARINE_VERSION_PARTS "${AQUAMARINE_VERSION}")
list(GET AQUAMARINE_VERSION_PARTS 0 AQUAMARINE_VERSION_MAJOR)
list(GET AQUAMARINE_VERSION_PARTS 1 AQUAMARINE_VERSION_MINOR)
list(GET AQUAMARINE_VERSION_PARTS 2 AQUAMARINE_VERSION_PATCH)
configure_file("${HYPRLAND_SOURCE_DIR}/src/version.h.in" "${HYPRLAND_SOURCE_DIR}/src/version.h" @ONLY)
include(${CMAKE_CURRENT_LIST_DIR}/Protocols.cmake)

# POSIX functions the MSVC CRT lacks (windows/compat/Posix.hpp).
add_library(hyprland_posix STATIC "${CMAKE_CURRENT_LIST_DIR}/../compat/Posix.cpp")
target_compile_options(hyprland_posix PRIVATE /W4 /EHsc /utf-8 /permissive-)
target_compile_definitions(hyprland_posix PRIVATE NOMINMAX)
target_include_directories(hyprland_posix PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../compat/include")
target_compile_options(hyprland_posix PRIVATE "/FI${HYPRLAND_PLATFORM_HEADER}")
target_link_libraries(hyprland_posix PRIVATE bcrypt advapi32)

# Stand-in for C++26 #embed in src/config/lua/DefaultConfig.hpp.
set(HYPRLAND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/upstream")
file(READ "${HYPRLAND_SOURCE_DIR}/example/hyprland.lua" HYPRLAND_EXAMPLE_LUA HEX)
# Character literals ('\xHH'), so bytes above 0x7f initialise a char array without narrowing.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," HYPRLAND_EXAMPLE_LUA "${HYPRLAND_EXAMPLE_LUA}")
file(CONFIGURE OUTPUT "${HYPRLAND_GENERATED_DIR}/ExampleConfigLua.inc" CONTENT "${HYPRLAND_EXAMPLE_LUA}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HYPRLAND_SOURCE_DIR}/example/hyprland.lua")

# Windows implementations that upstream sources call under #ifdef _WIN32. Kept out
# of src/ so Win32 headers never enter upstream translation units.
set(HYPRLAND_WINDOWS_PLATFORM_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/Spawn.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/MessagePump.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/Events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/ProcessInfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/ipc/Pipe.cpp")
# Platform halves of upstream classes; they need upstream headers, so they join the
# ported sources list as their upstream counterparts are ported.
set(HYPRLAND_WINDOWS_CLASS_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/ConfigWatcher.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/InitHelpers.cpp")

add_library(hyprland_core STATIC ${HYPRLAND_PORTED_SOURCES} ${HYPRLAND_WINDOWS_PLATFORM_SOURCES})
target_include_directories(hyprland_core PUBLIC "${HYPRLAND_SOURCE_DIR}/src" "${HYPRLAND_GENERATED_DIR}")
# NO_XWAYLAND: upstream's switch for builds without XWayland; Windows has no X server.
target_compile_definitions(hyprland_core PUBLIC HYPRLAND_VERSION="${HYPRLAND_VERSION}" NO_XWAYLAND NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
target_compile_features(hyprland_core PUBLIC cxx_std_23)
# Platform.hpp supplies POSIX vocabulary types to every upstream translation unit.
target_compile_options(hyprland_core PUBLIC /EHsc /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor "/FI${HYPRLAND_PLATFORM_HEADER}" PRIVATE /W1)
target_link_libraries(hyprland_core PUBLIC hyprland_posix hyprutils hyprlang hyprgraphics_color aquamarine libdrm_headers xkbcommon_headers libinput_headers wayland_server hyprland_protocols_interface unofficial::lua::lua re2::re2 muparser::muparser)

if(BUILD_TESTING)
    # Upstream unit tests (tests/) whose dependencies are all ported.
    set(HYPRLAND_PORTED_TESTS
        bell/RotatingPlayerThread.cpp
        desktop/rule/matchEngine/RegexMatchEngine.cpp
        desktop/rule/matchEngine/TagMatchEngine.cpp
        desktop/view/Focusable.cpp
        desktop/view/WindowFullscreenPolicy.cpp
        desktop/view/WindowMetadata.cpp
        helpers/ByteOperations.cpp
        helpers/CMType.cpp
        helpers/DamageRing.cpp
        helpers/DeformableMesh.cpp
        helpers/Direction.cpp
        helpers/TagKeeper.cpp
        layout/TiledGeometryMath.cpp
    )
    list(TRANSFORM HYPRLAND_PORTED_TESTS PREPEND "${HYPRLAND_SOURCE_DIR}/tests/")
    add_executable(hyprland_gtests ${HYPRLAND_PORTED_TESTS})
    target_compile_options(hyprland_gtests PRIVATE /W1)
    target_link_libraries(hyprland_gtests PRIVATE hyprland_core GTest::gtest_main)
    add_test(NAME hyprland_upstream_units COMMAND hyprland_gtests)
    set_tests_properties(hyprland_upstream_units PROPERTIES TIMEOUT 120)

    add_executable(wayland_server_tests "${CMAKE_CURRENT_LIST_DIR}/../tests/WaylandServerTests.cpp")
    target_compile_options(wayland_server_tests PRIVATE /W4 /EHsc /utf-8 /permissive- "/FI${HYPRLAND_PLATFORM_HEADER}")
    target_compile_definitions(wayland_server_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(wayland_server_tests PRIVATE cxx_std_23)
    target_link_libraries(wayland_server_tests PRIVATE wayland_server hyprland_posix GTest::gtest_main)
    add_test(NAME wayland_server_win32 COMMAND wayland_server_tests)
    set_tests_properties(wayland_server_win32 PROPERTIES TIMEOUT 60)

    add_executable(platform_tests "${CMAKE_CURRENT_LIST_DIR}/../tests/PlatformTests.cpp" ${HYPRLAND_WINDOWS_PLATFORM_SOURCES})
    target_compile_options(platform_tests PRIVATE /W4 /EHsc /utf-8 /permissive- "/FI${HYPRLAND_PLATFORM_HEADER}")
    target_compile_definitions(platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(platform_tests PRIVATE cxx_std_23)
    target_link_libraries(platform_tests PRIVATE hyprland_posix GTest::gtest_main)
    add_test(NAME windows_platform COMMAND platform_tests)
    set_tests_properties(windows_platform PROPERTIES TIMEOUT 60)

    add_executable(pipe_tests "${CMAKE_CURRENT_LIST_DIR}/../tests/PipeTests.cpp" "${CMAKE_CURRENT_LIST_DIR}/../platform/win32/ipc/Pipe.cpp")
    target_compile_options(pipe_tests PRIVATE /W4 /EHsc /utf-8 /permissive- "/FI${HYPRLAND_PLATFORM_HEADER}")
    target_compile_definitions(pipe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(pipe_tests PRIVATE cxx_std_23)
    target_link_libraries(pipe_tests PRIVATE wayland_server hyprland_posix advapi32 GTest::gtest_main)
    add_test(NAME windows_ipc_pipes COMMAND pipe_tests)
    set_tests_properties(windows_ipc_pipes PROPERTIES TIMEOUT 60)

    add_executable(aquamarine_win32_tests "${CMAKE_CURRENT_LIST_DIR}/../tests/AquamarineWin32Tests.cpp")
    target_compile_options(aquamarine_win32_tests PRIVATE /W4 /EHsc /utf-8 /permissive- /Zc:preprocessor "/FI${HYPRLAND_PLATFORM_HEADER}")
    target_compile_definitions(aquamarine_win32_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(aquamarine_win32_tests PRIVATE cxx_std_23)
    target_link_libraries(aquamarine_win32_tests PRIVATE aquamarine hyprland_posix GTest::gtest_main)
    add_test(NAME aquamarine_win32 COMMAND aquamarine_win32_tests)
    set_tests_properties(aquamarine_win32 PROPERTIES TIMEOUT 60)
endif()

# Porting survey: compiles every upstream src/ file with hyprland_core's flags so a
# keep-going build (ninja -k 0) shows each file's first blocker. See tools/port_survey.py.
option(HYPRLAND_PORT_SURVEY "Add the hyprland_port_survey target (all upstream src files)" OFF)
if(HYPRLAND_PORT_SURVEY)
    file(GLOB_RECURSE HYPRLAND_ALL_SOURCES CONFIGURE_DEPENDS "${HYPRLAND_SOURCE_DIR}/src/*.cpp")
    add_library(hyprland_port_survey STATIC EXCLUDE_FROM_ALL ${HYPRLAND_ALL_SOURCES} ${HYPRLAND_WINDOWS_CLASS_SOURCES})
    target_compile_options(hyprland_port_survey PRIVATE /W0)
    target_link_libraries(hyprland_port_survey PRIVATE hyprland_core)
    add_dependencies(hyprland_port_survey hyprland_protocol_headers)
endif()
