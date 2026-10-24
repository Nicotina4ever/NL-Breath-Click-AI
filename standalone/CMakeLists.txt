# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  BreathClick Studio — standalone Windows e CLI del motore condiviso
# ============================================================================
project(bc_studio LANGUAGES CXX)

# --- CLI: pipeline testabile senza GUI -------------------------------------
add_executable(bc_studio_cli studio_cli.cpp reaper_slice.cpp reaper_learning.cpp)

target_include_directories(bc_studio_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${THIRD_PARTY_DIR}/onnxruntime/include")

target_compile_definitions(bc_studio_cli PRIVATE UNICODE _UNICODE)
target_link_libraries(bc_studio_cli PRIVATE BreathClick::StudioEngine)
if(WIN32)
    target_link_libraries(bc_studio_cli PRIVATE bcrypt)
endif()
if(MSVC)
    target_link_options(bc_studio_cli PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()
set_target_properties(bc_studio_cli PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "studio")
bc_deploy_runtime_dependencies(bc_studio_cli)

# --- GUI: riusa WaveformView, AudioPlayer e AudioDocument ------------------
add_executable(bc_studio WIN32
    studio_main.cpp
    studio_dialogs.cpp
    studio_batch.cpp
    "${CMAKE_SOURCE_DIR}/editor/editor_project.cpp"
    "${CMAKE_SOURCE_DIR}/editor/editor_region_panel.cpp"
    "${CMAKE_SOURCE_DIR}/editor/detector_settings_store.cpp"
    "${CMAKE_SOURCE_DIR}/editor/detection_preview.cpp"
    "${CMAKE_SOURCE_DIR}/editor/detection_preview_worker.cpp"
    "${CMAKE_SOURCE_DIR}/editor/editor_window.cpp"
    "${CMAKE_SOURCE_DIR}/editor/waveform_view.cpp"
    "${CMAKE_SOURCE_DIR}/editor/audio_player.cpp"
    "${CMAKE_SOURCE_DIR}/assets/breathclick.rc")

target_include_directories(bc_studio PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${THIRD_PARTY_DIR}/onnxruntime/include"
    "${CMAKE_SOURCE_DIR}/editor")

target_compile_definitions(bc_studio PRIVATE UNICODE _UNICODE)
target_link_libraries(bc_studio PRIVATE BreathClick::StudioEngine)
if(WIN32)
    target_link_libraries(bc_studio PRIVATE comctl32 comdlg32 winmm bcrypt shell32 ole32)
endif()
if(MSVC)
    target_link_options(bc_studio PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()
set_target_properties(bc_studio PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "studio")
bc_deploy_runtime_dependencies(bc_studio)


# Porta accanto agli eseguibili il runtime ONNX e il modello Respiro-en.
set(BC_ONNX_DLL "${BC_ONNX_RUNTIME_FILE}")
set(BC_RESPIRO_MODEL "${BC_MODEL_DIR}/respiro-en-dinamico.onnx")
set(BC_RESPIRO_DATA "${BC_MODEL_DIR}/respiro-en-dinamico.onnx.data")
foreach(BC_TARGET bc_studio_cli bc_studio)
    if(EXISTS "${BC_ONNX_DLL}")
        add_custom_command(TARGET ${BC_TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${BC_ONNX_DLL}" "$<TARGET_FILE_DIR:${BC_TARGET}>/onnxruntime.dll")
    endif()
    if(EXISTS "${BC_RESPIRO_MODEL}")
        add_custom_command(TARGET ${BC_TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_FILE_DIR:${BC_TARGET}>/models/generale"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${BC_RESPIRO_MODEL}"
                    "$<TARGET_FILE_DIR:${BC_TARGET}>/models/generale/respiro-en-dinamico.onnx"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${BC_RESPIRO_DATA}"
                    "$<TARGET_FILE_DIR:${BC_TARGET}>/models/generale/respiro-en-dinamico.onnx.data")
    endif()
endforeach()

add_test(NAME bc_studio_cli_selftest COMMAND bc_studio_cli selftest)
set_tests_properties(bc_studio_cli_selftest PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_studio_cli>"
    TIMEOUT 600)

add_test(NAME bc_studio_session_selftest COMMAND bc_studio_cli session-selftest)
set_tests_properties(bc_studio_session_selftest PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_studio_cli>"
    TIMEOUT 120)

find_program(BC_PYTHON_EXECUTABLE NAMES python python3
    HINTS "${CMAKE_SOURCE_DIR}/../python_win")
# Fixture locali opzionali: nessun percorso K: obbligatorio.
set(BC_STUDIO_TEST_WORK "$ENV{BC_STUDIO_WORK}" CACHE PATH
    "Optional explicit review fixtures; default is synthetic build-local input" )
if(NOT BC_STUDIO_TEST_WORK)
    set(BC_STUDIO_TEST_WORK "${CMAKE_BINARY_DIR}/test_work")
endif()
if(BC_REQUIRE_RELEASE_TESTS)
    if(NOT BC_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "Release tests require Python with NumPy and SoundFile")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env PYTHONDONTWRITEBYTECODE=1
        "${BC_PYTHON_EXECUTABLE}" -c "import numpy, soundfile"
        RESULT_VARIABLE BC_PYTHON_TEST_STATUS)
    if(NOT BC_PYTHON_TEST_STATUS EQUAL 0)
        message(FATAL_ERROR "Release tests require working NumPy and SoundFile")
    endif()
endif()
if(BC_PYTHON_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_studio_pipeline.py")
    add_test(NAME bc_studio_portable_audio
        COMMAND "${BC_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_portable_audio.py")
    set_tests_properties(bc_studio_portable_audio PROPERTIES TIMEOUT 180)
    add_test(NAME bc_studio_pipeline_formats
        COMMAND "${BC_PYTHON_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_studio_pipeline.py")
    set_tests_properties(bc_studio_pipeline_formats PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 600)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_review_end_to_end.py")
        add_test(NAME bc_studio_review_e2e
            COMMAND "${BC_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_review_end_to_end.py")
        set_tests_properties(bc_studio_review_e2e PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 600)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_batch_review.py")
        add_test(NAME bc_studio_batch_review
            COMMAND "${BC_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_batch_review.py")
        set_tests_properties(bc_studio_batch_review PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 600)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_edit_engine.py")
        add_test(NAME bc_studio_edit_engine
            COMMAND "${BC_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_edit_engine.py")
        set_tests_properties(bc_studio_edit_engine PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 600)
    endif()
endif()

# Progetto multi-file: salvataggio/riapertura senza dialoghi, con due WAV
# sintetici e verifica che gli originali restino invariati.
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/project_test.ps1")
    add_test(NAME bc_studio_project_gui
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/project_test.ps1")
    set_tests_properties(bc_studio_project_gui PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 180)
endif()
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/workflow_test.ps1")
    add_test(NAME bc_studio_workflow
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/workflow_test.ps1")
    set_tests_properties(bc_studio_workflow PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 300)
endif()
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/gui_interaction_test.ps1")
    add_test(NAME bc_studio_gui_interaction
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/gui_interaction_test.ps1")
    set_tests_properties(bc_studio_gui_interaction PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 180)
endif()
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/performance_test.ps1")
    add_test(NAME bc_studio_performance
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/performance_test.ps1")
    set_tests_properties(bc_studio_performance PROPERTIES
        TIMEOUT 120
        FOLDER "editor")
endif()

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/detection_settings_test.ps1")
    add_test(NAME bc_studio_detection_settings
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/detection_settings_test.ps1")
    set_tests_properties(bc_studio_detection_settings PROPERTIES
        TIMEOUT 120
        FOLDER "editor")
endif()

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/editor/bulk_events_test.ps1")
    add_test(NAME bc_studio_bulk_events
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/editor/bulk_events_test.ps1")
    set_tests_properties(bc_studio_bulk_events PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 300)
endif()

# Test the selected build tree; never fall back to a pre-existing workspace binary.
add_test(NAME bc_studio_rc2_gui COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_SOURCE_DIR}/editor/rc2_workflow_test.ps1")
set_tests_properties(bc_studio_rc2_gui PROPERTIES TIMEOUT 180 RESOURCE_LOCK windows_gui)
get_property(BC_DIRECTORY_TESTS DIRECTORY PROPERTY TESTS)
foreach(BC_TEST IN LISTS BC_DIRECTORY_TESTS)
    set_property(TEST ${BC_TEST} APPEND PROPERTY ENVIRONMENT
        "BC_TEST_BIN=${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
        "BC_STUDIO_CLI=${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bc_studio_cli.exe"
        "BC_STUDIO_GEN=${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bc_cli.exe"
        "PYTHONDONTWRITEBYTECODE=1"
        "BC_STUDIO_WORK=${BC_STUDIO_TEST_WORK}")
    set_tests_properties(${BC_TEST} PROPERTIES FAIL_REGULAR_EXPRESSION "SKIP:;SKIPPED")
    if(BC_TEST MATCHES "^(editor_|bc_studio_(project_gui|workflow|gui_interaction|performance|detection_settings|bulk_events|portable_audio))")
        set_tests_properties(${BC_TEST} PROPERTIES RESOURCE_LOCK windows_gui)
    endif()
endforeach()
