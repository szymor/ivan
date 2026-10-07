# Dependencies for the SDL2 backend: the SDL2 headers (SDL.h) and library,
# picked up through SDL2's CMake config package (SDL2::SDL2 imported target).
# Linked into the shared ivan_build_flags interface so FeLib and the game
# both see the headers.
find_package(SDL2 2.0.0 REQUIRED)
target_link_libraries(ivan_build_flags INTERFACE SDL2::SDL2)
message(STATUS "IVAN: using SDL2 ${SDL2_VERSION}")