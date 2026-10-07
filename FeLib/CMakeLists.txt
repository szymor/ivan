# FeLib -- IVAN's front-end utility library.
# Implements the static library (libFeLib.a) of the original project layout.
#
# The source list must stay explicit: it is the list from the original
# Autotools libFeLib_a_SOURCES.

add_library(FeLib STATIC
  Source/bitmap.cpp
  Source/config.cpp
  Source/error.cpp
  Source/feio.cpp
  Source/felist.cpp
  Source/femain.cpp
  Source/femath.cpp
  Source/festring.cpp
  Source/fetime.cpp
  Source/graphics.cpp
  Source/hscore.cpp
  Source/rawbit.cpp
  Source/save.cpp
  Source/whandler.cpp
)

target_include_directories(FeLib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

# The platform-dependent half of graphics.cpp/whandler.cpp: exactly one
# backend folder is compiled, chosen by IVAN_BACKEND.
target_sources(FeLib PRIVATE
  Source/backends/${IVAN_BACKEND}/video.cpp
  Source/backends/${IVAN_BACKEND}/input.cpp
)

target_link_libraries(FeLib PUBLIC ivan_build_flags)
