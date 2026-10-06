# Iter Vehemens ad Necem (IVAN) -- CMake build system
#
# Port of the original GNU Autotools build (configure.in / Makefile.am), kept
# side by side with it until the CMake port is validated.

cmake_minimum_required(VERSION 3.20)

project(ivan VERSION 0.50 LANGUAGES CXX)

include(GNUInstallDirs)

# ----------------------------------------------------------------- options ---

option(IVAN_WIZARD "Enable wizard (cheat/debug) mode, as the old Windows builds did" OFF)
option(IVAN_INSTALL_PERMS
       "On install: additionally chown/chgrp the highscore file, Bones/ and the binary to the 'games' group and set the binary's setgid bit (needs root)"
       OFF)

# The game bakes these paths into the binary (game::GetGameDir() returns
# DATADIR "/ivan/", HIGH_SCORE_FILENAME is LOCAL_STATE_DIR "/ivan-highscore.scores"),
# so they must be absolute paths decided at configure time -- just like the old
# configure did with -DDATADIR / -DLOCAL_STATE_DIR.
set(IVAN_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}" CACHE PATH
    "Absolute directory whose ivan/ subdirectory holds Script/ and Graphics/ (compiled in as DATADIR)")
set(IVAN_LOCALSTATEDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/ivan" CACHE PATH
    "Absolute directory for the highscore file and Bones/ (compiled in as LOCAL_STATE_DIR)")

# The code is C++98 era, but it has always been built with whatever the
# compiler default happened to be (gnu++20 with GCC 16). Pinning a standard
# keeps future default bumps from breaking the build; 17 is old and stable
# enough to be supported by every compiler that can build this project, and
# the whole tree has been verified to compile with it.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# --------------------------------------------------------------------- SDL ---

# SDL 1.2 headers (SDL.h) and library; the module ships with CMake and
# provides the SDL::SDL imported target.
find_package(SDL 1.2 REQUIRED)
message(STATUS "IVAN: using SDL ${SDL_VERSION} (${SDL_LIBRARY})")

# ------------------------------------------------------- shared build flags ---

# Carried by every target: SDL include dirs/libraries plus the definitions the
# old configure.in put into CPPFLAGS (applied to FeLib and the game alike).
add_library(ivan_build_flags INTERFACE)

target_link_libraries(ivan_build_flags INTERFACE SDL::SDL)

target_compile_definitions(ivan_build_flags INTERFACE
  USE_SDL
  "DATADIR=\"${IVAN_DATADIR}\""
  "LOCAL_STATE_DIR=\"${IVAN_LOCALSTATEDIR}\""
)

if(IVAN_WIZARD)
  target_compile_definitions(ivan_build_flags INTERFACE WIZARD)
endif()

if(UNIX)
  target_compile_definitions(ivan_build_flags INTERFACE LINUX _GNU_SOURCE=1 _REENTRANT)
endif()

if(WIN32)
  target_compile_definitions(ivan_build_flags INTERFACE WIN32)
endif()

if(MSVC)
  target_compile_definitions(ivan_build_flags INTERFACE VC)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # felibdef.h uses this for NO_ALIGNMENT / NO_RETURN / LIKE_PRINTF.
  target_compile_definitions(ivan_build_flags INTERFACE GCC)
endif()

# ------------------------------------------------------------------ targets ---

add_subdirectory(FeLib)
add_subdirectory(Main)

# ------------------------------------------------- runnable-from-build-tree ---

# When the paths above point inside the build tree (see the "dev" preset) the
# game can be run without installing: set up the same data/state layout that
# "make install" would have created.
set(_ivan_dev FALSE)
foreach(_ivan_path IN ITEMS "${IVAN_DATADIR}" "${IVAN_LOCALSTATEDIR}")
  string(FIND "${_ivan_path}" "${CMAKE_BINARY_DIR}" _ivan_pos)
  if(_ivan_pos EQUAL 0)
    set(_ivan_dev TRUE)
  endif()
endforeach()

if(_ivan_dev)
  file(MAKE_DIRECTORY "${IVAN_DATADIR}/ivan")
  foreach(_ivan_dir Script Graphics)
    if(NOT EXISTS "${IVAN_DATADIR}/ivan/${_ivan_dir}")
      file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/${_ivan_dir}"
                       "${IVAN_DATADIR}/ivan/${_ivan_dir}" SYMBOLIC)
    endif()
  endforeach()

  # The game only creates Bones/ under $HOME-relative paths on Linux, so give
  # it a writable state directory and an (empty, like "touch") highscore file.
  file(MAKE_DIRECTORY "${IVAN_LOCALSTATEDIR}/Bones")
  if(NOT EXISTS "${IVAN_LOCALSTATEDIR}/ivan-highscore.scores")
    file(WRITE "${IVAN_LOCALSTATEDIR}/ivan-highscore.scores" "")
  endif()

  message(STATUS "IVAN: dev layout in build tree -- data: ${IVAN_DATADIR}/ivan, state: ${IVAN_LOCALSTATEDIR}")
endif()

# ----------------------------------------------------------------- install ---

# Data files, as in Script/Makefile.am and Graphics/Makefile.am.
install(DIRECTORY Script/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ivan/Script
        FILES_MATCHING PATTERN "*.dat")

install(DIRECTORY Graphics/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/ivan/Graphics
        FILES_MATCHING PATTERN "*.pcx" PATTERN "*.bmp")

# The old install-data-local rule: the state-directory layout (Bones/ and the
# highscore file) is always set up on UNIX, best-effort; chown/chgrp to the
# "games" group and the setgid binary bit are optional (IVAN_INSTALL_PERMS)
# because they need root.
if(UNIX)
  configure_file(cmake/InstallPermissions.cmake.in
                 ${CMAKE_BINARY_DIR}/cmake/InstallPermissions.cmake @ONLY)
  install(SCRIPT ${CMAKE_BINARY_DIR}/cmake/InstallPermissions.cmake)
elseif(IVAN_INSTALL_PERMS)
  message(WARNING "IVAN_INSTALL_PERMS is only supported on UNIX, ignoring it.")
endif()
