# Dependencies for the SDL3 backend: SDL3's CMake config package
# (SDL3::SDL3 imported target). SDL3 also keeps SDL_LoadBMP() in its core
# (used for the window icon), so no SDL3_image dependency is needed.
#
# SDL3's imported targets only expose <prefix>/include, but the SDL3 headers
# live in <prefix>/include/SDL3/ -- there is no top-level SDL.h in SDL3.
# Add the SDL3 subdir to the interface includes so the legacy "#include
# "SDL.h"" in femain.cpp/error.cpp keeps resolving.
find_package(SDL3 3.0.0 REQUIRED)
target_link_libraries(ivan_build_flags INTERFACE SDL3::SDL3)
find_path(_sdl3_headers_dir SDL.h PATH_SUFFIXES SDL3)
target_include_directories(ivan_build_flags INTERFACE "${_sdl3_headers_dir}")
message(STATUS "IVAN: using SDL3 ${SDL3_VERSION}")