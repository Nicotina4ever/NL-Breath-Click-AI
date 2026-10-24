# ============================================================================
#  LAME 3.100 — encoder MP3, LGPL 2.0
#
#  Costruito dai sorgenti ufficiali (lame-3.100) per avere un binario
#  riproducibile e della stessa architettura/CRT del programma.
#  La DLL viene distribuita con BreathClick; il testo LGPL e i sorgenti sono
#  in questa cartella. Il pacchetto include anche LAME_SOURCE_OFFER.txt.
#
#  Non compiliamo il decoder mpglib: BreathClick decodifica con dr_mp3 e usa
#  LAME solo per la codifica.
# ============================================================================
project(bc_lame C)

set(LAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

set(LAME_SOURCES
    "${LAME_ROOT}/libmp3lame/VbrTag.c"
    "${LAME_ROOT}/libmp3lame/bitstream.c"
    "${LAME_ROOT}/libmp3lame/encoder.c"
    "${LAME_ROOT}/libmp3lame/fft.c"
    "${LAME_ROOT}/libmp3lame/gain_analysis.c"
    "${LAME_ROOT}/libmp3lame/id3tag.c"
    "${LAME_ROOT}/libmp3lame/lame.c"
    "${LAME_ROOT}/libmp3lame/newmdct.c"
    "${LAME_ROOT}/libmp3lame/presets.c"
    "${LAME_ROOT}/libmp3lame/psymodel.c"
    "${LAME_ROOT}/libmp3lame/quantize.c"
    "${LAME_ROOT}/libmp3lame/quantize_pvt.c"
    "${LAME_ROOT}/libmp3lame/reservoir.c"
    "${LAME_ROOT}/libmp3lame/set_get.c"
    "${LAME_ROOT}/libmp3lame/tables.c"
    "${LAME_ROOT}/libmp3lame/takehiro.c"
    "${LAME_ROOT}/libmp3lame/util.c"
    "${LAME_ROOT}/libmp3lame/vbrquantize.c"
    "${LAME_ROOT}/libmp3lame/version.c"
    )

add_library(mp3lame SHARED ${LAME_SOURCES} "${LAME_ROOT}/mp3lame.def")

target_include_directories(mp3lame PRIVATE
    "${LAME_ROOT}/libmp3lame"
    "${LAME_ROOT}"
    "${LAME_ROOT}/include")

target_compile_definitions(mp3lame PRIVATE
    HAVE_CONFIG_H
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(mp3lame PRIVATE /W0 /MP)
endif()

target_link_libraries(mp3lame PRIVATE winmm)

set_target_properties(mp3lame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "libmp3lame"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "third_party")
