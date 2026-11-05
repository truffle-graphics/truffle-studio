# Truffle, SDL3 and OpenCASCADE are Git submodules under external/ and build from source.

function(studio_require_submodule path marker hint)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${path}/${marker}")
        message(FATAL_ERROR "Missing ${path}. Run:\n  ${hint}")
    endif()
endfunction()

studio_require_submodule(external/truffle CMakeLists.txt
    "git submodule update --init external/truffle")
studio_require_submodule(external/SDL CMakeLists.txt
    "git submodule update --init external/SDL")
studio_require_submodule(external/occt CMakeLists.txt
    "git submodule update --init external/occt")
foreach(marker freetype/CMakeLists.txt harfbuzz/CMakeLists.txt
               sheenbidi/Source/SheenBidi.c utf8proc/utf8proc.c)
    studio_require_submodule(external/truffle/vendor ${marker}
        "git -C external/truffle submodule update --init vendor/freetype vendor/harfbuzz vendor/sheenbidi vendor/utf8proc")
endforeach()

# Truffle: only the libraries Studio links, Metal presentation and GUI text.
set(TFL_BUILD_BACKEND_METAL ON CACHE BOOL "" FORCE)
set(TFL_GUI_ENABLE_TEXT ON CACHE BOOL "" FORCE)
# The 3D view compiles its MSL program at startup.
set(TFL_SHADER_ENABLE_METAL_COMPILER ON CACHE BOOL "" FORCE)
set(TFL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TFL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TFL_INSTALL OFF CACHE BOOL "" FORCE)
set(TFL_BUILD_RHI_DOCTOR OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/external/truffle" external/truffle EXCLUDE_FROM_ALL)

# SDL3 as a static library; Studio only needs video, events and Metal views.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/external/SDL" external/SDL EXCLUDE_FROM_ALL)

include(StudioOcct)

function(studio_target_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(STUDIO_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
