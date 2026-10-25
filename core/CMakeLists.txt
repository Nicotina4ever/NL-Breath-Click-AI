# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  core — motore DSP, rilevamento e editing
#
#  Nessuna dipendenza da ONNX Runtime. Il gate vocale e' la salienza armonica,
#  calcolata qui dentro per ogni frame: un modello in meno significa nessuna
#  DLL da caricare, nessun provider da scegliere, e niente che possa funzionare
#  su una macchina e non su un'altra.
# ============================================================================
project(ai_breathclick_core LANGUAGES CXX)

set(CORE_SOURCES
    # dsp
    src/dsp/frame_features.cpp
    src/dsp/resampler.cpp
    src/dsp/breath_detector.cpp
    src/dsp/silence_gate.cpp
    src/dsp/click_detector.cpp
    src/dsp/click_repair.cpp
    # rilevamento
    src/detect/offline_analyzer.cpp
    src/detect/detector_settings_codec.cpp
    src/detect/segment_features.cpp
    src/detect/candidates.cpp
    src/model/model_profile.cpp
    src/model/regions_json.cpp
    # editing
    src/edit/wav_io.cpp
    src/edit/audio_io.cpp
    src/edit/region_editor.cpp
    src/edit/audio_document.cpp
    src/learning/personal_profile.cpp
    # utilita'
    src/util/runtime_paths.cpp
    src/util/logger.cpp
    src/util/atomic_file.cpp
    src/util/json_validation.cpp
    src/force_link.cpp
)

add_library(ai_breathclick_core STATIC ${CORE_SOURCES})
add_library(AI::Core ALIAS ai_breathclick_core)

set(BC_PRIVATE_MODEL_DIR "" CACHE PATH "Optional private legacy coefficient headers; never distributed")
if(BC_PRIVATE_MODEL_DIR)
    foreach(header breath_model.h breath_model_net.h breath_model_net_narrow.h)
        if(NOT EXISTS "${BC_PRIVATE_MODEL_DIR}/${header}")
            message(FATAL_ERROR "Missing private legacy model: ${BC_PRIVATE_MODEL_DIR}/${header}")
        endif()
    endforeach()
    target_include_directories(ai_breathclick_core PUBLIC "${BC_PRIVATE_MODEL_DIR}")
    target_compile_definitions(ai_breathclick_core PUBLIC BC_PRIVATE_LEGACY_MODELS=1)
endif()


target_include_directories(ai_breathclick_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nl_breathclick>"
    PRIVATE
        "${THIRD_PARTY_DIR}/dr_libs"
        "${THIRD_PARTY_DIR}/lame/include"
)

target_link_libraries(ai_breathclick_core PUBLIC kissfft Threads::Threads PRIVATE ${CMAKE_DL_LIBS})

if(WIN32)
    target_link_libraries(ai_breathclick_core PUBLIC bcrypt)
    target_compile_definitions(ai_breathclick_core PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(ai_breathclick_core PRIVATE OpenSSL::Crypto)
endif()

set_target_properties(ai_breathclick_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "core")

# ---------------------------------------------------------------------------
#  Copia delle DLL necessarie accanto a un eseguibile o plugin.
#
#  Resta come funzione vuota perche' CLI, plugin, estensione e test la
#  chiamano: senza runtime esterni non c'e' piu' nulla da copiare, ma il punto
#  di aggancio rimane se in futuro servisse di nuovo.
# ---------------------------------------------------------------------------
function(bc_deploy_runtime_dependencies TARGET)
    if(NOT WIN32 AND BC_LAME_LIBRARY)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BC_LAME_LIBRARY}"
            "$<TARGET_FILE_DIR:${TARGET}>/${CMAKE_SHARED_LIBRARY_PREFIX}mp3lame${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
endfunction()
