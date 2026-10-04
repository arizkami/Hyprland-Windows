# cmake "-DPATCHES=a.patch|b.patch" -P ApplyPatch.cmake, run inside the source tree.
# Idempotent: an already-applied patch is left alone, so re-populating is safe.
string(REPLACE "|" ";" PATCHES "${PATCHES}")
foreach(patch IN LISTS PATCHES)
    execute_process(COMMAND git apply --reverse --check "${patch}" RESULT_VARIABLE already OUTPUT_QUIET ERROR_QUIET)
    if(already EQUAL 0)
        continue()
    endif()
    execute_process(COMMAND git apply --whitespace=nowarn "${patch}" RESULT_VARIABLE applied)
    if(NOT applied EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${patch}")
    endif()
endforeach()
