# The IVAN game executable.
# Was: Main/Source/Makefile.am -> bin_PROGRAMS = ivan
#
# Do NOT glob this directory. Only the 25 files below are compiled; the other
# 39 *.cpp files in Source/ are textually #included by the *set.cpp files
# (charset.cpp does #include "char.cpp", itemset.cpp includes item.cpp, ...),
# exactly as ivan_SOURCES / EXTRA_DIST split them in the old Makefile.am.
# A glob would compile those twice and fail at link time with duplicate
# symbols.

add_executable(ivan
  Source/actset.cpp
  Source/areaset.cpp
  Source/charset.cpp
  Source/charsset.cpp
  Source/command.cpp
  Source/coreset.cpp
  Source/dataset.cpp
  Source/dungeon.cpp
  Source/game.cpp
  Source/godset.cpp
  Source/iconf.cpp
  Source/id.cpp
  Source/igraph.cpp
  Source/itemset.cpp
  Source/levelset.cpp
  Source/main.cpp
  Source/materset.cpp
  Source/message.cpp
  Source/object.cpp
  Source/roomset.cpp
  Source/script.cpp
  Source/slotset.cpp
  Source/trapset.cpp
  Source/wmapset.cpp
  Source/wskill.cpp
)

target_include_directories(ivan PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_link_libraries(ivan PRIVATE FeLib)

# Keep the binary at the top of the build tree (build/ivan), not build/Main/.
set_target_properties(ivan PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(WIN32)
  # Windowed application with the icon, like Main.dsp / ivanmgw.mak did.
  target_sources(ivan PRIVATE Resource/Ivan.rc)
  set_target_properties(ivan PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS ivan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
