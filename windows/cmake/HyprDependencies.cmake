# hypr* libraries at the revisions upstream Hyprland pins in flake.lock, each with
# its Windows patches from windows/patches/<name>/. Upstream CMakeLists.txt files
# target Linux/BSD (pkg-config, GCC flags), so library targets are defined here.
include(FetchContent)

set(HYPR_WINDOWS_PATCH_DIR "${CMAKE_CURRENT_LIST_DIR}/../patches")
set(HYPR_THIRD_PARTY_OPTIONS /W1 /EHsc /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor)

function(hypr_fetch name repository revision)
    file(GLOB patches "${HYPR_WINDOWS_PATCH_DIR}/${name}/*.patch")
    set(patch_command "")
    if(patches)
        list(SORT patches)
        # '|' keeps the list intact inside the PATCH_COMMAND argument list.
        string(REPLACE ";" "|" patches "${patches}")
        set(patch_command PATCH_COMMAND ${CMAKE_COMMAND} "-DPATCHES=${patches}" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ApplyPatch.cmake")
    endif()
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG ${revision}
        GIT_CONFIG core.autocrlf=false
        UPDATE_DISCONNECTED TRUE
        ${patch_command}
        # Upstream CMakeLists.txt is not added; targets are defined below.
        SOURCE_SUBDIR windows-no-cmake)
    FetchContent_MakeAvailable(${name})
    set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(hypr_version name out)
    file(READ "${${name}_SOURCE_DIR}/VERSION" raw)
    string(STRIP "${raw}" version)
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

find_package(GTest CONFIG REQUIRED)
find_path(PIXMAN_INCLUDE_DIR pixman.h PATH_SUFFIXES pixman-1 REQUIRED)
find_library(PIXMAN_LIBRARY NAMES pixman-1 pixman-1_static REQUIRED)
add_library(pixman::pixman UNKNOWN IMPORTED)
set_target_properties(pixman::pixman PROPERTIES IMPORTED_LOCATION "${PIXMAN_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${PIXMAN_INCLUDE_DIR}")

# hyprutils ------------------------------------------------------------------
hypr_fetch(hyprutils https://github.com/hyprwm/hyprutils.git 95983ee836ff205e615bba67d1f67098e1231941)
hypr_version(hyprutils HYPRUTILS_VERSION)
file(GLOB_RECURSE HYPRUTILS_SOURCES CONFIGURE_DEPENDS "${hyprutils_SOURCE_DIR}/src/*.cpp")
list(FILTER HYPRUTILS_SOURCES EXCLUDE REGEX "eventLoop/backend/(Epoll|Kqueue)\\.cpp$")
add_library(hyprutils STATIC ${HYPRUTILS_SOURCES})
target_include_directories(hyprutils PUBLIC "${hyprutils_SOURCE_DIR}/include" PRIVATE "${hyprutils_SOURCE_DIR}/src")
target_compile_definitions(hyprutils PRIVATE HYPRUTILS_VERSION="${HYPRUTILS_VERSION}" WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(hyprutils PUBLIC cxx_std_23)
target_compile_options(hyprutils PRIVATE ${HYPR_THIRD_PARTY_OPTIONS})
target_link_libraries(hyprutils PUBLIC pixman::pixman)

# hyprlang -------------------------------------------------------------------
hypr_fetch(hyprlang https://github.com/hyprwm/hyprlang.git 9508458be316a0d70d37ebed1ab725ccd10411ff)
hypr_version(hyprlang HYPRLANG_VERSION)
file(GLOB HYPRLANG_SOURCES CONFIGURE_DEPENDS "${hyprlang_SOURCE_DIR}/src/*.cpp")
add_library(hyprlang STATIC ${HYPRLANG_SOURCES})
target_include_directories(hyprlang PUBLIC "${hyprlang_SOURCE_DIR}/include")
target_compile_definitions(hyprlang PRIVATE HYPRLANG_VERSION="${HYPRLANG_VERSION}" HYPRLANG_INTERNAL WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(hyprlang PRIVATE ${HYPR_THIRD_PARTY_OPTIONS})
target_link_libraries(hyprlang PUBLIC hyprutils)

# hyprgraphics: colour math only so far. Image/cairo/EGL parts arrive with rendering.
hypr_fetch(hyprgraphics https://github.com/hyprwm/hyprgraphics.git 7c895c44e3ca6d28ed68ddd80ec02b02b925e7fc)
add_library(hyprgraphics_color STATIC "${hyprgraphics_SOURCE_DIR}/src/color/Color.cpp")
target_include_directories(hyprgraphics_color PUBLIC "${hyprgraphics_SOURCE_DIR}/include")
target_compile_features(hyprgraphics_color PUBLIC cxx_std_23)
target_compile_options(hyprgraphics_color PRIVATE ${HYPR_THIRD_PARTY_OPTIONS})

# libdrm: headers only. Format code shares DRM fourcc codes and mode structs.
FetchContent_Declare(libdrm
    GIT_REPOSITORY https://gitlab.freedesktop.org/mesa/drm.git
    GIT_TAG libdrm-2.4.125
    GIT_SHALLOW TRUE
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR windows-no-cmake)
FetchContent_MakeAvailable(libdrm)
add_library(libdrm_headers INTERFACE)
target_include_directories(libdrm_headers INTERFACE "${libdrm_SOURCE_DIR}" "${libdrm_SOURCE_DIR}/include/drm" "${CMAKE_CURRENT_LIST_DIR}/../compat/include")

# aquamarine: the backend-independent core plus the Win32 backend from
# windows/patches/aquamarine. DRM, GBM, Wayland, headless and the libseat session
# depend on Linux kernel interfaces and are not built.
hypr_fetch(aquamarine https://github.com/hyprwm/aquamarine.git 04bfb7db7ef7d5b2cc6860623396c606ef015e96)
hypr_version(aquamarine AQUAMARINE_VERSION)
file(GLOB_RECURSE AQUAMARINE_SOURCES CONFIGURE_DEPENDS "${aquamarine_SOURCE_DIR}/src/*.cpp")
list(FILTER AQUAMARINE_SOURCES EXCLUDE REGEX "src/backend/(drm/|Wayland|Headless|Session)|src/allocator/(GBM|DRMDumb)")
add_library(aquamarine STATIC ${AQUAMARINE_SOURCES})
target_include_directories(aquamarine PUBLIC "${aquamarine_SOURCE_DIR}/include" PRIVATE "${aquamarine_SOURCE_DIR}/src" "${aquamarine_SOURCE_DIR}/src/include")
target_compile_definitions(aquamarine PRIVATE AQUAMARINE_VERSION="${AQUAMARINE_VERSION}" WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(aquamarine PRIVATE ${HYPR_THIRD_PARTY_OPTIONS} "/FI${CMAKE_CURRENT_LIST_DIR}/../compat/Platform.hpp")
target_link_libraries(aquamarine PUBLIC hyprutils libdrm_headers PRIVATE shcore)

if(BUILD_TESTING)
    file(GLOB_RECURSE HYPRUTILS_TESTS CONFIGURE_DEPENDS "${hyprutils_SOURCE_DIR}/tests/*.cpp")
    # These exercise fork/poll/eventfd; tests/win32 covers the Windows implementations.
    list(FILTER HYPRUTILS_TESTS EXCLUDE REGEX "tests/(eventLoop/EventLoop|os/Fd|os/ProcLock|os/Process)\\.cpp$")
    add_executable(hyprutils_tests ${HYPRUTILS_TESTS})
    target_include_directories(hyprutils_tests PRIVATE "${hyprutils_SOURCE_DIR}/src")
    # Upstream runs these in Debug: EXPECT_DEBUG_DEATH needs NDEBUG unset.
    target_compile_options(hyprutils_tests PRIVATE ${HYPR_THIRD_PARTY_OPTIONS} /UNDEBUG)
    target_compile_definitions(hyprutils_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(hyprutils_tests PRIVATE hyprutils GTest::gtest_main)
    add_test(NAME hyprutils COMMAND hyprutils_tests)
    set_tests_properties(hyprutils PROPERTIES TIMEOUT 120)

    add_executable(hyprlang_tests "${hyprlang_SOURCE_DIR}/tests/parse/main.cpp")
    target_compile_options(hyprlang_tests PRIVATE ${HYPR_THIRD_PARTY_OPTIONS} /UNDEBUG)
    target_link_libraries(hyprlang_tests PRIVATE hyprlang)
    add_test(NAME hyprlang COMMAND hyprlang_tests parse WORKING_DIRECTORY "${hyprlang_SOURCE_DIR}/tests")
    set_tests_properties(hyprlang PROPERTIES TIMEOUT 60)
endif()
