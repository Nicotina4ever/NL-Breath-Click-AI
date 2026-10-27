# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  tests — verifica automatica del motore
# ============================================================================
project(bc_tests LANGUAGES CXX)

if(WIN32 AND BC_BUILD_STUDIO)
    add_executable(bc_audio_player_tests test_audio_player.cpp "${CMAKE_SOURCE_DIR}/editor/audio_player.cpp")
    target_include_directories(bc_audio_player_tests PRIVATE "${CMAKE_SOURCE_DIR}")
    target_link_libraries(bc_audio_player_tests PRIVATE winmm Threads::Threads)
    set_target_properties(bc_audio_player_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME bc_audio_player_tests COMMAND bc_audio_player_tests)
    set_tests_properties(bc_audio_player_tests PROPERTIES TIMEOUT 30 RESOURCE_LOCK windows_gui)
endif()

add_executable(bc_analysis_control_tests test_analysis_control.cpp)
target_include_directories(bc_analysis_control_tests PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(bc_analysis_control_tests PRIVATE BreathClick::StudioEngine)
set_target_properties(bc_analysis_control_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
bc_deploy_runtime_dependencies(bc_analysis_control_tests)
add_test(NAME bc_analysis_control_tests COMMAND bc_analysis_control_tests)
set_tests_properties(bc_analysis_control_tests PROPERTIES TIMEOUT 90)

add_executable(bc_preview_tests test_detection_preview.cpp
    "${CMAKE_SOURCE_DIR}/editor/detection_preview_worker.cpp")
target_include_directories(bc_preview_tests PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/core/include")
target_link_libraries(bc_preview_tests PRIVATE ai_breathclick_core)
set_target_properties(bc_preview_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
bc_deploy_runtime_dependencies(bc_preview_tests)
add_test(NAME bc_preview_tests COMMAND bc_preview_tests)
set_tests_properties(bc_preview_tests PROPERTIES TIMEOUT 60)

add_executable(bc_persistence_tests test_persistence.cpp)
target_include_directories(bc_persistence_tests PRIVATE "${CMAKE_SOURCE_DIR}"
    "${THIRD_PARTY_DIR}/onnxruntime/include")
target_link_libraries(bc_persistence_tests PRIVATE BreathClick::StudioEngine)
set_target_properties(bc_persistence_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME bc_persistence_tests COMMAND bc_persistence_tests)
set_tests_properties(bc_persistence_tests PROPERTIES TIMEOUT 60)

add_executable(bc_rc2_tests test_rc2_workflow.cpp)
target_include_directories(bc_rc2_tests PRIVATE "${CMAKE_SOURCE_DIR}" "${THIRD_PARTY_DIR}/onnxruntime/include")
target_link_libraries(bc_rc2_tests PRIVATE BreathClick::StudioEngine)
set_target_properties(bc_rc2_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME bc_rc2_tests COMMAND bc_rc2_tests)
set_tests_properties(bc_rc2_tests PROPERTIES TIMEOUT 60)

add_executable(bc_tests test_core.cpp)

target_include_directories(bc_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(bc_tests PRIVATE ai_breathclick_core)

if(MSVC)
    target_link_options(bc_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

set_target_properties(bc_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")

bc_deploy_runtime_dependencies(bc_tests)

add_test(NAME bc_tests COMMAND bc_tests)
set_tests_properties(bc_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_tests>"
    TIMEOUT 600)

# ---------------------------------------------------------------------------
#  Prove del documento dell'editor (modello di editing e cronologia)
# ---------------------------------------------------------------------------
add_executable(bc_document_tests test_document.cpp)

target_include_directories(bc_document_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(bc_document_tests PRIVATE ai_breathclick_core)

if(MSVC)
    target_link_options(bc_document_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

set_target_properties(bc_document_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")

bc_deploy_runtime_dependencies(bc_document_tests)

add_test(NAME bc_document_tests COMMAND bc_document_tests)
set_tests_properties(bc_document_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_document_tests>"
    TIMEOUT 600)

# ---------------------------------------------------------------------------
#  Rilevatore di silenzio con isteresi
# ---------------------------------------------------------------------------
add_executable(bc_silence_tests test_silence.cpp)

target_include_directories(bc_silence_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(bc_silence_tests PRIVATE ai_breathclick_core)

if(MSVC)
    target_link_options(bc_silence_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

set_target_properties(bc_silence_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")

bc_deploy_runtime_dependencies(bc_silence_tests)

add_test(NAME bc_silence_tests COMMAND bc_silence_tests)
set_tests_properties(bc_silence_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_silence_tests>"
    TIMEOUT 120)

# ---------------------------------------------------------------------------
#  Feature dei segmenti e candidati larghi
# ---------------------------------------------------------------------------
add_executable(bc_feature_tests test_features.cpp)

target_include_directories(bc_feature_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(bc_feature_tests PRIVATE ai_breathclick_core)

if(MSVC)
    target_link_options(bc_feature_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

set_target_properties(bc_feature_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")

bc_deploy_runtime_dependencies(bc_feature_tests)

add_test(NAME bc_feature_tests COMMAND bc_feature_tests)
set_tests_properties(bc_feature_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_feature_tests>"
    TIMEOUT 120)

# ---------------------------------------------------------------------------
#  Codec: WAV -> MP3 -> PCM e sniffing per contenuto.
# ---------------------------------------------------------------------------
add_executable(bc_codec_tests test_codec.cpp)
target_include_directories(bc_codec_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")
target_link_libraries(bc_codec_tests PRIVATE ai_breathclick_core)
if(MSVC)
    target_link_options(bc_codec_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()
set_target_properties(bc_codec_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")
bc_deploy_runtime_dependencies(bc_codec_tests)
add_test(NAME bc_codec_tests COMMAND bc_codec_tests)
set_tests_properties(bc_codec_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_codec_tests>"
    TIMEOUT 120)

# ---------------------------------------------------------------------------
#  Apprendimento e riconciliazione: nessuna etichetta automatica.
# ---------------------------------------------------------------------------
add_executable(bc_learning_tests test_learning.cpp)
target_include_directories(bc_learning_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")
target_link_libraries(bc_learning_tests PRIVATE ai_breathclick_core)
if(MSVC)
    target_link_options(bc_learning_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()
set_target_properties(bc_learning_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")
bc_deploy_runtime_dependencies(bc_learning_tests)
add_test(NAME bc_learning_tests COMMAND bc_learning_tests)
set_tests_properties(bc_learning_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_learning_tests>"
    TIMEOUT 120)

# ---------------------------------------------------------------------------
#  Store impostazioni/preset: persistenza reale senza toccare AppData nei test.
# ---------------------------------------------------------------------------
add_executable(bc_settings_store_tests
    test_detector_settings_store.cpp
    "${CMAKE_SOURCE_DIR}/editor/detector_settings_store.cpp")
target_include_directories(bc_settings_store_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/editor")
target_link_libraries(bc_settings_store_tests PRIVATE ai_breathclick_core)
set_target_properties(bc_settings_store_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")
bc_deploy_runtime_dependencies(bc_settings_store_tests)
add_test(NAME bc_settings_store_tests COMMAND bc_settings_store_tests)
set_tests_properties(bc_settings_store_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_settings_store_tests>"
    TIMEOUT 60)

# ---------------------------------------------------------------------------
#  Progetto multi-file: roundtrip, percorsi relativi e riscrittura atomica.
# ---------------------------------------------------------------------------
add_executable(bc_project_tests test_project.cpp)

target_include_directories(bc_project_tests PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/standalone")

target_link_libraries(bc_project_tests PRIVATE BreathClick::StudioEngine)
if(MSVC)
    target_link_options(bc_project_tests PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()
set_target_properties(bc_project_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tests")
bc_deploy_runtime_dependencies(bc_project_tests)

add_test(NAME bc_project_tests COMMAND bc_project_tests)
set_tests_properties(bc_project_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_project_tests>"
    TIMEOUT 120)

add_executable(bc_platform_tests test_platform.cpp)
target_link_libraries(bc_platform_tests PRIVATE BreathClick::StudioEngine)
set_target_properties(bc_platform_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
bc_deploy_runtime_dependencies(bc_platform_tests)
add_test(NAME bc_platform_tests COMMAND bc_platform_tests)
set_tests_properties(bc_platform_tests PROPERTIES TIMEOUT 30)
