# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.16)
project(nl_audio_suite_consumer LANGUAGES CXX)
find_package(NLBreathClick 1 CONFIG REQUIRED)
add_executable(nl_suite_engine main.cpp)
target_link_libraries(nl_suite_engine PRIVATE NL::BreathClickEngine)
