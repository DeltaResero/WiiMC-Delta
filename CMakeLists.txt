# WiiMC, built with devkitPro's Wii toolchain:
#
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake
#   make -j$(nproc)
#
# This gives wiimc.dol. -DWIIMC_CCACHE=ON compiles through ccache, FFmpeg,
# MPlayer, and libiconv included. "make run" sends the DOL to a Wii running
# the Homebrew Channel (WIILOAD=tcp:<address>).
cmake_minimum_required(VERSION 3.26)
project(wiimc C CXX ASM)
include(ExternalProject)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
  message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake")
endif()

option(WIIMC_CCACHE "Compile through ccache" OFF)
option(WIIMC_NTFS "Read NTFS drives" ON)
option(WIIMC_EXT2 "Read ext2/3/4 drives (needs libext2fs from portlibs)" OFF)

set(LIBSRC ${CMAKE_SOURCE_DIR}/src/lib)
set(MPLAYER ${CMAKE_SOURCE_DIR}/src/mplayer)
set(LIBOGC_INC ${DEVKITPRO}/libogc/include)
set(LIBOGC_LIB ${DEVKITPRO}/libogc/lib/wii)
# for the libraries that build with their own scripts and makefiles
set(MACHDEP -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float)
# FFmpeg, libiconv, and mbedTLS go into this prefix, where the others find them
set(EXTPREFIX ${CMAKE_BINARY_DIR}/external)
file(MAKE_DIRECTORY ${EXTPREFIX}/include ${EXTPREFIX}/lib)

# __FILE__ names sources relative to the repository, not the build machine
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=)

if(WIIMC_CCACHE)
  find_program(CCACHE ccache REQUIRED)
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
  set(EXT_CC "${CCACHE} powerpc-eabi-gcc")
endif()

# The other build systems run with devkitPPC on the PATH. Under a Makefile
# generator they join make's job server; otherwise they use every core.
set(EXT_ENV ${CMAKE_COMMAND} -E env
  "PATH=${DEVKITPRO}/portlibs/wii/bin:${DEVKITPRO}/portlibs/ppc/bin:${DEVKITPRO}/devkitPPC/bin:$ENV{PATH}")
if(CMAKE_GENERATOR MATCHES "Makefiles")
  set(SUBMAKE "$(MAKE)")
else()
  cmake_host_system_information(RESULT JOBS QUERY NUMBER_OF_LOGICAL_CORES)
  set(SUBMAKE make -j${JOBS})
endif()

#---------------------------------------------------------------------------------
# libntfs
#---------------------------------------------------------------------------------
file(GLOB NTFS_SRC CONFIGURE_DEPENDS ${LIBSRC}/libntfs/source/*.c)
add_library(ntfs STATIC ${NTFS_SRC})
target_compile_definitions(ntfs PRIVATE HAVE_CONFIG_H)
target_compile_options(ntfs PRIVATE -O2 -Wall -Wno-address-of-packed-member
  -fno-asynchronous-unwind-tables)
target_include_directories(ntfs PUBLIC ${LIBSRC}/libntfs/include)

#---------------------------------------------------------------------------------
# mbedTLS, configured by its committed include/mbedtls/mbedtls_config.h, in
# the three archives upstream builds; libmbedcrypto also gets the Wii's
# clock and entropy source. FFmpeg's configure looks for them in EXTPREFIX.
#---------------------------------------------------------------------------------
set(MBEDTLS ${LIBSRC}/mbedtls)
file(GLOB MBEDTLS_SRC CONFIGURE_DEPENDS ${MBEDTLS}/library/*.c)
set(MBEDTLS_X509 ${MBEDTLS_SRC})
list(FILTER MBEDTLS_X509 INCLUDE REGEX "/(x509[^/]*|pkcs7)\\.c$")
set(MBEDTLS_TLS ${MBEDTLS_SRC})
list(FILTER MBEDTLS_TLS INCLUDE REGEX "/(ssl[^/]*|mps[^/]*|debug|net_sockets)\\.c$")
set(MBEDTLS_CRYPTO ${MBEDTLS_SRC})
list(REMOVE_ITEM MBEDTLS_CRYPTO ${MBEDTLS_X509} ${MBEDTLS_TLS})
list(APPEND MBEDTLS_CRYPTO ${LIBSRC}/mbedtls-wii/platform_wii.c)

file(GLOB MBEDTLS_HEADERS CONFIGURE_DEPENDS ${MBEDTLS}/include/mbedtls/*.h ${MBEDTLS}/include/psa/*.h)
add_custom_command(OUTPUT ${EXTPREFIX}/include/mbedtls/mbedtls_config.h
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${EXTPREFIX}/include/mbedtls ${EXTPREFIX}/include/psa
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${MBEDTLS}/include/mbedtls ${EXTPREFIX}/include/mbedtls
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${MBEDTLS}/include/psa ${EXTPREFIX}/include/psa
  DEPENDS ${MBEDTLS_HEADERS}
  VERBATIM)
add_custom_target(mbedtls_headers DEPENDS ${EXTPREFIX}/include/mbedtls/mbedtls_config.h)

foreach(part crypto x509 tls)
  string(TOUPPER ${part} PART)
  add_library(mbed${part} STATIC ${MBEDTLS_${PART}})
  target_compile_options(mbed${part} PRIVATE -O2 -Wall -fno-asynchronous-unwind-tables)
  target_include_directories(mbed${part} PRIVATE ${MBEDTLS}/include)
  set_target_properties(mbed${part} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${EXTPREFIX}/lib)
  add_dependencies(mbed${part} mbedtls_headers)
endforeach()

#---------------------------------------------------------------------------------
# libiconv: only libcharset and lib/ are built
#---------------------------------------------------------------------------------
list(JOIN MACHDEP " " machdep)
set(ICONV_CONFIGURE --quiet --host=powerpc-eabi --prefix=${EXTPREFIX}
  --disable-shared --enable-static --disable-nls
  "CFLAGS=-O2 ${machdep} -fno-asynchronous-unwind-tables")
if(WIIMC_CCACHE)
  list(APPEND ICONV_CONFIGURE "CC=${EXT_CC}")
endif()
ExternalProject_Add(iconv
  SOURCE_DIR ${LIBSRC}/libiconv
  BINARY_DIR ${CMAKE_BINARY_DIR}/libiconv
  CONFIGURE_COMMAND ${EXT_ENV} <SOURCE_DIR>/configure ${ICONV_CONFIGURE}
  BUILD_COMMAND ${EXT_ENV} ${SUBMAKE} lib/localcharset.h
    COMMAND ${EXT_ENV} ${SUBMAKE} -C lib
  INSTALL_COMMAND ${EXT_ENV} ${SUBMAKE} -C lib install-lib
      libdir=${EXTPREFIX}/lib includedir=${EXTPREFIX}/include
    COMMAND ${CMAKE_COMMAND} -E copy include/iconv.h.inst ${EXTPREFIX}/include/iconv.h
  BUILD_BYPRODUCTS ${EXTPREFIX}/lib/libiconv.a
  LOG_CONFIGURE TRUE
  LOG_OUTPUT_ON_FAILURE TRUE)

#---------------------------------------------------------------------------------
# FFmpeg 9 (src/lib/ffmpeg), with the options in cmake/FFmpeg.cmake. It is
# configured again when they change, and its own makefiles decide what to
# rebuild, so its build step runs every time. Its install rewrites every
# file, so it installs into a staging folder and only the files that changed
# are copied on; otherwise MPlayer would rebuild and WiiMC relink each time.
#---------------------------------------------------------------------------------
set(FFMPEG_STAGE ${CMAKE_BINARY_DIR}/ffmpeg-install)
include(cmake/FFmpeg.cmake)
if(WIIMC_CCACHE)
  list(APPEND FFMPEG_CONFIGURE "--cc=${EXT_CC}")
endif()
set(FFMPEG_LIBS)
foreach(lib avformat avcodec swscale swresample avutil)
  list(APPEND FFMPEG_LIBS ${EXTPREFIX}/lib/lib${lib}.a)
endforeach()
ExternalProject_Add(ffmpeg
  SOURCE_DIR ${LIBSRC}/ffmpeg
  BINARY_DIR ${CMAKE_BINARY_DIR}/ffmpeg
  CONFIGURE_COMMAND ${EXT_ENV} <SOURCE_DIR>/configure ${FFMPEG_CONFIGURE}
  BUILD_COMMAND ${EXT_ENV} ${SUBMAKE}
  INSTALL_COMMAND ${EXT_ENV} ${SUBMAKE} install
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${FFMPEG_STAGE}/include ${EXTPREFIX}/include
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${FFMPEG_STAGE}/lib ${EXTPREFIX}/lib
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS ${FFMPEG_LIBS}
  DEPENDS mbedcrypto mbedx509 mbedtls
  LOG_CONFIGURE TRUE
  LOG_OUTPUT_ON_FAILURE TRUE)
# keep the configure line, with its local paths, out of the binary
ExternalProject_Add_Step(ffmpeg strip-configuration
  COMMAND sed -i "s/^#define FFMPEG_CONFIGURATION .*/#define FFMPEG_CONFIGURATION \"\"/" config.h
  WORKING_DIRECTORY <BINARY_DIR>
  DEPENDEES configure
  DEPENDERS build)

#---------------------------------------------------------------------------------
# MPlayer, with its own Makefile and the committed config.h and config.mak,
# building in place in src/mplayer. Its Makefile decides what to rebuild, so
# it runs every time. "make mplayer-clean" removes its objects.
#---------------------------------------------------------------------------------
set(MPLAYER_MAKE ${SUBMAKE} DEVKITPRO=${DEVKITPRO} EXTPREFIX=${EXTPREFIX})
if(WIIMC_CCACHE)
  list(APPEND MPLAYER_MAKE "CC=${EXT_CC}")
endif()
ExternalProject_Add(mplayer
  SOURCE_DIR ${MPLAYER}
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${EXT_ENV} ${MPLAYER_MAKE}
  INSTALL_COMMAND ""
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS ${MPLAYER}/libmplayerwii.a
  DEPENDS ffmpeg iconv)
add_custom_target(mplayer-clean
  COMMAND ${EXT_ENV} make -C ${MPLAYER} wiiclean
  VERBATIM)

#---------------------------------------------------------------------------------
# Embedded files, each with a header giving its symbols and size
#---------------------------------------------------------------------------------
find_program(BIN2S bin2s HINTS ${DEVKITPRO}/tools/bin REQUIRED)

# wiimc_embed(<objects var> <headers var> <output dir> <files>...)
function(wiimc_embed objects headers dir)
  file(MAKE_DIRECTORY ${dir})
  set(asm)
  set(hdr)
  foreach(file IN LISTS ARGN)
    get_filename_component(name ${file} NAME)
    string(REPLACE "." "_" symbol ${name})
    add_custom_command(OUTPUT ${dir}/${symbol}.s ${dir}/${symbol}.h
      COMMAND sh -c "\"$0\" -a 32 -H \"$1\" \"$2\" > \"$3\""
        ${BIN2S} ${dir}/${symbol}.h ${file} ${dir}/${symbol}.s
      DEPENDS ${file}
      WORKING_DIRECTORY ${dir}
      VERBATIM)
    list(APPEND asm ${dir}/${symbol}.s)
    list(APPEND hdr ${dir}/${symbol}.h)
  endforeach()
  set(${objects} ${asm} PARENT_SCOPE)
  set(${headers} ${hdr} PARENT_SCOPE)
endfunction()

# Fonts, images, and gzipped language files (gettext.cpp unpacks the chosen one)
set(EMBED_DIR ${CMAKE_BINARY_DIR}/embedded)
file(MAKE_DIRECTORY ${EMBED_DIR})
set(EMBED_FILES)
foreach(dir src/images src/lang src/fonts)
  foreach(ext ttf lang png jpg)
    file(GLOB files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${dir}/*.${ext})
    foreach(file IN LISTS files)
      if(file MATCHES "\\.lang$")
        get_filename_component(name ${file} NAME)
        add_custom_command(OUTPUT ${EMBED_DIR}/${name}.gz
          COMMAND sh -c "gzip -9 -n -c \"$0\" > \"$1\"" ${file} ${EMBED_DIR}/${name}.gz
          DEPENDS ${file}
          VERBATIM)
        set(file ${EMBED_DIR}/${name}.gz)
      endif()
      list(APPEND EMBED_FILES ${file})
    endforeach()
  endforeach()
endforeach()
wiimc_embed(EMBED_ASM EMBED_HEADERS ${EMBED_DIR} ${EMBED_FILES})
add_library(wiimc_data OBJECT ${EMBED_ASM})

#---------------------------------------------------------------------------------
# WiiMC
#---------------------------------------------------------------------------------
set(SOURCES)
foreach(dir src src/libwiigui src/utils src/utils/unzip)
  file(GLOB files CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/${dir}/*.cpp ${CMAKE_SOURCE_DIR}/${dir}/*.c)
  list(APPEND SOURCES ${files})
endforeach()
add_executable(wiimc $<TARGET_OBJECTS:wiimc_data> ${SOURCES})
set_target_properties(wiimc PROPERTIES LINKER_LANGUAGE CXX)

# A changed embedded file can change its size, which the code takes from
# its header, so every source waits for and depends on all the headers
set_source_files_properties(${SOURCES} PROPERTIES OBJECT_DEPENDS "${EMBED_HEADERS}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

target_compile_options(wiimc PRIVATE -g -O3 -Wall -Wframe-larger-than=8192
  "SHELL:-iquote ${CMAKE_SOURCE_DIR}/src" "SHELL:-iquote ${MPLAYER}")
target_compile_definitions(wiimc PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
target_include_directories(wiimc PRIVATE ${EMBED_DIR} ${EXTPREFIX}/include)
target_include_directories(wiimc SYSTEM PRIVATE
  ${DEVKITPRO}/portlibs/wii/include ${DEVKITPRO}/portlibs/ppc/include)

set(WIIMC_FS_LIBS)
if(WIIMC_NTFS)
  target_compile_definitions(wiimc PRIVATE WANT_NTFS)
  list(APPEND WIIMC_FS_LIBS ntfs)
endif()
if(WIIMC_EXT2)
  target_compile_definitions(wiimc PRIVATE WANT_EXT2)
  list(APPEND WIIMC_FS_LIBS ext2fs)
endif()

# src/utils/tls.c shares one parsed CA bundle with FFmpeg's connections, and
# src/networkop.cpp runs host name lookups one at a time
target_link_options(wiimc PRIVATE -g -Wl,-Map,${CMAKE_BINARY_DIR}/wiimc.map
  -Wl,--wrap=mbedtls_x509_crt_parse_file,--wrap=mbedtls_ssl_conf_ca_chain
  -Wl,--wrap=net_gethostbyname)
target_link_libraries(wiimc PRIVATE
  ${MPLAYER}/libmplayerwii.a ${FFMPEG_LIBS}
  mbedtls mbedx509 mbedcrypto
  jpeg di iso9660 ${EXTPREFIX}/lib/libiconv.a png z mxml PkgConfig::FREETYPE
  fat wiiuse bte ogc tinysmb ${WIIMC_FS_LIBS})
add_dependencies(wiimc mplayer ffmpeg iconv)

ogc_create_dol(wiimc)

# the channel forwarder, built only when asked for: "make forwarder"
add_subdirectory(extras/forwarder EXCLUDE_FROM_ALL)

add_custom_target(run
  COMMAND ${DEVKITPRO}/tools/bin/wiiload ${CMAKE_BINARY_DIR}/wiimc.dol
  DEPENDS wiimc
  VERBATIM)
add_custom_target(reload
  COMMAND ${DEVKITPRO}/tools/bin/wiiload -r ${CMAKE_BINARY_DIR}/wiimc.dol
  DEPENDS wiimc
  VERBATIM)
