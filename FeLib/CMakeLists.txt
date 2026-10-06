# FeLib -- IVAN's front-end utility library.
# Was: FeLib/Source/Makefile.am -> noinst_LIBRARIES = libFeLib.a
#
# The source list must stay explicit: it is the list from libFeLib_a_SOURCES.

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

target_link_libraries(FeLib PUBLIC ivan_build_flags)
