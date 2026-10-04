# Selects the vcpkg toolchain before project(). Native dependencies come from
# windows/vcpkg.json. Order: an explicit CMAKE_TOOLCHAIN_FILE, VCPKG_ROOT, then
# the copy bundled with Visual Studio.
if(NOT CMAKE_HOST_WIN32 OR DEFINED CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Directory containing vcpkg.json")
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "Static libraries, dynamic CRT")

set(_hyprland_vcpkg_root "")
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(_hyprland_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
    set(_hyprland_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
    if(EXISTS "${_hyprland_vswhere}")
        execute_process(
            COMMAND "${_hyprland_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.Vcpkg -property installationPath
            OUTPUT_VARIABLE _hyprland_vs_root OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_hyprland_vs_root AND EXISTS "${_hyprland_vs_root}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(_hyprland_vcpkg_root "${_hyprland_vs_root}/VC/vcpkg")
        endif()
    endif()
endif()

if(NOT _hyprland_vcpkg_root)
    message(FATAL_ERROR "vcpkg not found. Install the Visual Studio \"vcpkg package manager\" component, set VCPKG_ROOT, or pass -DCMAKE_TOOLCHAIN_FILE.")
endif()
file(TO_CMAKE_PATH "${_hyprland_vcpkg_root}" _hyprland_vcpkg_root)
set(CMAKE_TOOLCHAIN_FILE "${_hyprland_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
message(STATUS "Using vcpkg from ${_hyprland_vcpkg_root}")
