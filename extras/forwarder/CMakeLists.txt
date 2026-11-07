# The channel forwarder: boot.dol, which starts WiiMC from a Wii Menu channel.
# It builds with WiiMC's libntfs: "make forwarder" in the build folder.
file(GLOB FORWARDER_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.c)
file(GLOB FORWARDER_PNG CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/images/*.png)
wiimc_embed(FORWARDER_ASM FORWARDER_HEADERS ${CMAKE_CURRENT_BINARY_DIR}/embedded ${FORWARDER_PNG})

add_executable(forwarder ${FORWARDER_SRC} ${FORWARDER_ASM})
set_target_properties(forwarder PROPERTIES OUTPUT_NAME boot)
set_source_files_properties(${FORWARDER_SRC} PROPERTIES OBJECT_DEPENDS "${FORWARDER_HEADERS}")

target_compile_options(forwarder PRIVATE -g -O2 -Wall)
target_compile_definitions(forwarder PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _LARGEFILE64_SOURCE)
target_include_directories(forwarder PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/embedded)
target_include_directories(forwarder SYSTEM PRIVATE
  ${DEVKITPRO}/portlibs/wii/include ${DEVKITPRO}/portlibs/ppc/include)

# linked high in memory, out of the way of the DOL it loads
target_link_options(forwarder PRIVATE -g -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/boot.map
  -Wl,--section-start,.init=0x81230000)
target_link_libraries(forwarder PRIVATE ogc fat ntfs png m z)

ogc_create_dol(forwarder)
