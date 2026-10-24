# SPDX-License-Identifier: MPL-2.0
find_program(BC_PYTHON_EXECUTABLE NAMES python python3 HINTS "${CMAKE_SOURCE_DIR}/../python_win")
if(NOT BC_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "REAPER adapter tests require Python (or configure BC_BUILD_REAPER_TESTS=OFF)")
endif()
execute_process(COMMAND "${BC_PYTHON_EXECUTABLE}" -c "import numpy, soundfile; from lupa.lua54 import LuaRuntime"
    RESULT_VARIABLE BC_REAPER_TEST_DEPS OUTPUT_QUIET ERROR_QUIET)
if(NOT BC_REAPER_TEST_DEPS EQUAL 0)
    message(FATAL_ERROR "REAPER tests require NumPy, SoundFile and Lupa/Lua 5.4; no packages are installed automatically")
endif()
add_test(NAME bc_reaper_lua COMMAND "${BC_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_lua.py")
if(BC_BUILD_STUDIO_CLI)
    add_test(NAME bc_reaper_bridge COMMAND "${BC_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py")
    set_tests_properties(bc_reaper_bridge PROPERTIES
        ENVIRONMENT "BC_TEST_BIN=$<TARGET_FILE_DIR:bc_studio_cli>;PYTHONDONTWRITEBYTECODE=1" TIMEOUT 120)
endif()
set_tests_properties(bc_reaper_lua PROPERTIES
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 60)

if(BC_BUILD_STUDIO_CLI AND WIN32)
    add_test(NAME bc_reaper_portable_worker COMMAND "${BC_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py")
    set_tests_properties(bc_reaper_portable_worker PROPERTIES
        ENVIRONMENT "BC_TEST_BIN=$<TARGET_FILE_DIR:bc_studio_cli>;BC_PORTABLE_WORKER=1;PYTHONDONTWRITEBYTECODE=1" TIMEOUT 120)
endif()
