# SPDX-License-Identifier: MPL-2.0
# ============================================================================
#  cli — strumento offline bc_cli
# ============================================================================
project(bc_cli LANGUAGES CXX)

set(CLI_SOURCES main.cpp)
if(WIN32)
    list(APPEND CLI_SOURCES "${CMAKE_SOURCE_DIR}/assets/breathclick.rc")
endif()

add_executable(bc_cli ${CLI_SOURCES})

target_include_directories(bc_cli PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/core/include")

target_link_libraries(bc_cli PRIVATE ai_breathclick_core)

if(MSVC)
    target_link_options(bc_cli PRIVATE "/WHOLEARCHIVE:ai_breathclick_core")
endif()

set_target_properties(bc_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    FOLDER "tools")

bc_deploy_runtime_dependencies(bc_cli)

# Nessun modello da copiare: il rilevamento e' DSP puro.
# Test di verifica funzionale: se il selftest fallisce, la build fallisce.
add_test(NAME bc_cli_selftest COMMAND bc_cli selftest)
set_tests_properties(bc_cli_selftest PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:bc_cli>"
    TIMEOUT 600)
