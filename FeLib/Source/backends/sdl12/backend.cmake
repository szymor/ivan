# Dependencies for the SDL 1.2 backend: the SDL 1.2 headers (SDL.h) and
# library. The module ships with CMake and provides the SDL::SDL imported
# target. Linked into the shared ivan_build_flags interface so FeLib and
# the game both see the headers.
find_package(SDL 1.2 REQUIRED)
target_link_libraries(ivan_build_flags INTERFACE SDL::SDL)
message(STATUS "IVAN: using SDL ${SDL_VERSION} (${SDL_LIBRARY})")