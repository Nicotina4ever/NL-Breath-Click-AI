# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.16)

project(AI_BreathClick
    VERSION 1.0.0
    DESCRIPTION "Riduzione automatica di respiri e click in registrazioni vocali"
    LANGUAGES C CXX)

# ============================================================================
#  Impostazioni globali
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

enable_testing()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

# ============================================================================
#  Percorsi delle dipendenze
#  I percorsi possono essere sovrascritti da variabili d'ambiente, cosi' il
#  progetto si compila anche se le dipendenze sono altrove.
# ============================================================================
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "Dipendenze esterne verificate")
set(BC_MODEL_DIR "${CMAKE_SOURCE_DIR}/models/generale" CACHE PATH "Modello Respiro-en verificato")
set(BC_ONNX_RUNTIME_FILE "${THIRD_PARTY_DIR}/onnxruntime/onnxruntime.dll" CACHE FILEPATH "Optional pinned ONNX runtime DLL")
set(BC_STUDIO_VERSION "${PROJECT_VERSION}-rc.3")
set(BC_SOURCE_COMMIT "unknown" CACHE STRING "Public source commit used to build release binaries")
configure_file(shared/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
option(BC_REQUIRE_RELEASE_TESTS "Richiedi tutte le dipendenze dei test di release" OFF)

# ============================================================================
#  Opzioni
# ============================================================================
# ---------------------------------------------------------------------------
#  Componenti attivi di NL Breath Click Studio.
#  VST3 ed estensione C++ REAPER non fanno parte della build corrente:
#  REAPER usa script Lua e il bridge CLI, senza un'estensione nativa.
# ---------------------------------------------------------------------------
option(BC_BUILD_CLI       "Compila la CLI offline e le utilita' di test" ON)
if(WIN32)
    set(BC_GUI_DEFAULT ON)
else()
    set(BC_GUI_DEFAULT OFF)
endif()
option(BC_BUILD_EDITOR    "Compila la GUI/editor condivisa" ${BC_GUI_DEFAULT})
option(BC_BUILD_STUDIO "Compila la GUI Windows" ${BC_GUI_DEFAULT})
option(BC_BUILD_STUDIO_CLI "Compila il backend CLI indipendente" ON)
option(BC_BUILD_ENGINE "Build the shared C++ engine even without applications" ON)
option(BC_BUILD_TESTS     "Compila i test"                              ON)
option(BC_BUILD_REAPER_TESTS "Verifica adapter REAPER con mock Lua e bridge audio" ON)

if(BC_REQUIRE_RELEASE_TESTS AND (NOT BC_BUILD_CLI OR NOT BC_BUILD_EDITOR OR NOT BC_BUILD_STUDIO OR NOT BC_BUILD_STUDIO_CLI OR NOT BC_BUILD_TESTS))
    message(FATAL_ERROR "Release validation requires CLI, editor, Studio and all tests")
endif()

if(NOT BC_BUILD_CLI AND NOT BC_BUILD_STUDIO AND NOT BC_BUILD_STUDIO_CLI AND NOT BC_BUILD_ENGINE AND NOT BC_BUILD_EDITOR)
    message(FATAL_ERROR "Nessun componente selezionato: abilitare almeno BC_BUILD_CLI o BC_BUILD_STUDIO")
endif()

# ============================================================================
#  kissfft — compilato dai sorgenti
#  In precedenza veniva linkata una libreria precompilata (kissfft-float.lib)
#  e FFTW3, che non era utilizzata da nessuna riga di codice. Compilarla dai
#  sorgenti elimina una dipendenza binaria fragile e garantisce coerenza di
#  architettura e CRT.
# ============================================================================
add_library(kissfft STATIC
    "${THIRD_PARTY_DIR}/kissfft/kiss_fft.c"
    "${THIRD_PARTY_DIR}/kissfft/kiss_fftr.c")

target_include_directories(kissfft PUBLIC
    "$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/kissfft>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nl_breathclick/third_party/kissfft>")

if(MSVC)
    # kissfft e' C: alcuni avvisi di conversione non sono azionabili qui.
    target_compile_options(kissfft PRIVATE /W0)
else()
    target_compile_options(kissfft PRIVATE -w)
endif()

set_target_properties(kissfft PROPERTIES FOLDER "third_party")

# ============================================================================
if(WIN32)
    add_subdirectory("${THIRD_PARTY_DIR}/lame" "${CMAKE_BINARY_DIR}/third_party/lame")
else()
    find_library(BC_LAME_LIBRARY NAMES mp3lame REQUIRED)
    add_library(mp3lame SHARED IMPORTED GLOBAL)
    set_target_properties(mp3lame PROPERTIES IMPORTED_LOCATION "${BC_LAME_LIBRARY}")
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
endif()
if(NOT WIN32 AND (BC_BUILD_EDITOR OR BC_BUILD_STUDIO))
    message(FATAL_ERROR "The current GUI is Windows-only; headless Core/CLI are portable. Disable BC_BUILD_EDITOR and BC_BUILD_STUDIO.")
endif()

# ============================================================================
# Compiler policy must precede our subdirectories: directory options set
# afterwards do not reach their targets (notably UTF-8 source literals).
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /wd4101 /wd4244 /wd4267 /wd4996 /MP)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_subdirectory(core)
if(BC_BUILD_ENGINE OR BC_BUILD_EDITOR OR BC_BUILD_STUDIO OR BC_BUILD_STUDIO_CLI OR BC_BUILD_TESTS)
    add_subdirectory(standalone/engine)
endif()

if(BC_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BC_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(BC_BUILD_STUDIO OR BC_BUILD_STUDIO_CLI)
    add_subdirectory(standalone)
endif()

if(BC_BUILD_TESTS)
    add_subdirectory(tests)
    if(BC_BUILD_REAPER_TESTS)
        add_subdirectory(reaper/tests)
    endif()
endif()

foreach(bc_codec_target bc_studio bc_studio_cli bc_editor bc_codec_tests)
    if(TARGET ${bc_codec_target} AND TARGET mp3lame)
        add_dependencies(${bc_codec_target} mp3lame)
    endif()
endforeach()

# ============================================================================
#  Riepilogo
# ============================================================================
message(STATUS "")
message(STATUS "================ Riepilogo configurazione ================")
message(STATUS "  Versione          : ${BC_STUDIO_VERSION}")
message(STATUS "  CLI offline       : ${BC_BUILD_CLI}")
message(STATUS "  Editor waveform   : ${BC_BUILD_EDITOR}")
message(STATUS "  Test              : ${BC_BUILD_TESTS}")
message(STATUS "  Accelerazione NPU : rimossa (non funzionante)")
message(STATUS "  Inferenza         : DSP CPU; Respiro-en ONNX opzionale")
message(STATUS "==========================================================")
message(STATUS "")

# A single C++ engine package for standalone, REAPER backends and NL Audio Suite.
# Never install private generated coefficient headers or private SDK builds.
if(TARGET bc_studio_engine AND NOT BC_PRIVATE_MODEL_DIR)
    set_target_properties(bc_studio_engine PROPERTIES EXPORT_NAME BreathClickEngine)
    set_target_properties(ai_breathclick_core PROPERTIES EXPORT_NAME BreathClickCore)
    set_target_properties(kissfft PROPERTIES EXPORT_NAME BreathClickFFT)
    install(TARGETS bc_studio_engine ai_breathclick_core kissfft EXPORT NLBreathClickTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY core/include/core DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nl_breathclick
        PATTERN "breath_model.h" EXCLUDE PATTERN "breath_model_net.h" EXCLUDE PATTERN "breath_model_net_narrow.h" EXCLUDE)
    install(FILES standalone/studio_engine.h standalone/studio_project.h standalone/studio_session.h standalone/respiro_onnx.h standalone/analysis_control.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nl_breathclick)
    install(FILES "${THIRD_PARTY_DIR}/kissfft/kiss_fft.h" "${THIRD_PARTY_DIR}/kissfft/kiss_fftr.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nl_breathclick/third_party/kissfft)
    install(EXPORT NLBreathClickTargets NAMESPACE NL:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NLBreathClick)
    configure_package_config_file(cmake/NLBreathClickConfig.cmake.in
        "${CMAKE_BINARY_DIR}/NLBreathClickConfig.cmake" INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NLBreathClick)
    write_basic_package_version_file("${CMAKE_BINARY_DIR}/NLBreathClickConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_BINARY_DIR}/NLBreathClickConfig.cmake" "${CMAKE_BINARY_DIR}/NLBreathClickConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NLBreathClick)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/nl_breathclick)
    install(DIRECTORY LICENSES DESTINATION ${CMAKE_INSTALL_DATADIR}/nl_breathclick)
endif()
