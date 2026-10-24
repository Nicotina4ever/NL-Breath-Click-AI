# SPDX-License-Identifier: MPL-2.0
# Headless Studio pipeline, compiled once for GUI, CLI and native tests.
# No Win32 controls, playback device or application settings in this target.
add_library(bc_studio_engine STATIC
    ../studio_engine.cpp
    ../respiro_onnx.cpp
    ../studio_session.cpp
    ../studio_project.cpp)
add_library(BreathClick::StudioEngine ALIAS bc_studio_engine)
target_include_directories(bc_studio_engine
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${THIRD_PARTY_DIR}/onnxruntime/include")
target_link_libraries(bc_studio_engine PUBLIC AI::Core)
if(WIN32)
    target_link_libraries(bc_studio_engine PUBLIC bcrypt)
endif()
set_target_properties(bc_studio_engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "studio")
