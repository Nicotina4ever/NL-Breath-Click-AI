# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  editor — editor waveform standalone
# ============================================================================
project(bc_editor LANGUAGES CXX)

set(EDITOR_SOURCES
    main.cpp
    editor_window.cpp
    editor_project.cpp
    editor_region_panel.cpp
    detector_settings_store.cpp
    detection_preview.cpp
    detection_preview_worker.cpp
    waveform_view.cpp
    audio_player.cpp
    "${CMAKE_SOURCE_DIR}/standalone/studio_dialogs.cpp"
    "${CMAKE_SOURCE_DIR}/standalone/studio_batch.cpp")

# L'icona e' una risorsa Windows: senza il file .rc l'eseguibile resta con
# quella predefinita di sistema.
if(WIN32)
    list(APPEND EDITOR_SOURCES "${CMAKE_SOURCE_DIR}/assets/breathclick.rc")
endif()

add_executable(bc_editor WIN32 ${EDITOR_SOURCES})

target_compile_definitions(bc_editor PRIVATE UNICODE _UNICODE)
target_include_directories(bc_editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/standalone"
    "${THIRD_PARTY_DIR}/onnxruntime/include")

target_link_libraries(bc_editor PRIVATE BreathClick::StudioEngine)

if(MSVC)
    target_link_options(bc_editor PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

# comctl32 e comdlg32 sono richiesti da commctrl.h e commdlg.h; winmm da waveOut.
if(WIN32)
    target_link_libraries(bc_editor PRIVATE comctl32 comdlg32 winmm bcrypt shell32)
# I profili storici sono dati protetti e servono ai test dell'editor.
add_custom_command(TARGET bc_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:bc_editor>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/models"
            "$<TARGET_FILE_DIR:bc_editor>/models"
    COMMENT "Copia profili modello per l'editor")

endif()

set_target_properties(bc_editor PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "editor")

bc_deploy_runtime_dependencies(bc_editor)

# ---------------------------------------------------------------------------
#  Prova di avvio: l'editor viene davvero eseguito su un file reale e deve
#  restare vivo con la finestra aperta. E' l'unico modo per accorgersi di un
#  crash in fase di costruzione dell'interfaccia, che i test del motore non
#  possono vedere.
# ---------------------------------------------------------------------------
if(BC_BUILD_CLI AND WIN32)
    add_test(NAME editor_smoke
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.ps1"
                "$<TARGET_FILE:bc_editor>" "$<TARGET_FILE:bc_cli>")
    set_tests_properties(editor_smoke PROPERTIES
        TIMEOUT 180
        FOLDER "editor")

    # Prova FUNZIONALE: non si limita a verificare che la finestra si apra,
    # preme davvero i pulsanti (BM_CLICK) e trascina col mouse, poi controlla
    # che il programma reagisca.
    #
    # Serve perche' i pulsanti erano creati con ID diversi da quelli che
    # onCommand() riconosce: ogni clic cadeva nel `default:` senza alcun errore,
    # e lo smoke test - che guardava solo i controlli esistenti - non se ne
    # accorgeva.
    add_test(NAME editor_functional
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/functional_test.ps1")
    set_tests_properties(editor_functional PROPERTIES
        TIMEOUT 300
        FOLDER "editor")

    # Il cursore del guadagno: si muove? l'etichetta segue? il documento cambia?
    add_test(NAME editor_gain
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/test_guadagno.ps1")
    set_tests_properties(editor_gain PROPERTIES
        TIMEOUT 180
        FOLDER "editor")

    # Multi-traccia: piu' file aperti, cambio traccia.
    add_test(NAME editor_tracce
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/test_tracce.ps1")
    set_tests_properties(editor_tracce PROPERTIES
        TIMEOUT 180
        FOLDER "editor")

    # Profili di modello: elenco, selezione, ritorno al base.
    add_test(NAME editor_profili
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/test_profili.ps1")
    set_tests_properties(editor_profili PROPERTIES
        TIMEOUT 180
        FOLDER "editor")
endif()

# Test the selected build tree; never fall back to a pre-existing workspace binary.
get_property(BC_DIRECTORY_TESTS DIRECTORY PROPERTY TESTS)
foreach(BC_TEST IN LISTS BC_DIRECTORY_TESTS)
    set_property(TEST ${BC_TEST} APPEND PROPERTY ENVIRONMENT
        "BC_TEST_BIN=${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
        "BC_STUDIO_CLI=${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bc_studio_cli.exe"
        "BC_STUDIO_GEN=${CMAKE_BINARY_DIR}/bin/$<CONFIG>/bc_cli.exe"
        "PYTHONDONTWRITEBYTECODE=1")
    set_tests_properties(${BC_TEST} PROPERTIES FAIL_REGULAR_EXPRESSION "SKIP:;SKIPPED")
    if(BC_TEST MATCHES "^(editor_|bc_studio_(project_gui|workflow|gui_interaction|performance|detection_settings|bulk_events|portable_audio))")
        set_tests_properties(${BC_TEST} PROPERTIES RESOURCE_LOCK windows_gui)
    endif()
endforeach()
