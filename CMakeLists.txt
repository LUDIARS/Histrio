cmake_minimum_required(VERSION 4.2)
project(Histrio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
if(MSVC)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.52)
    message(FATAL_ERROR "Histrio requires MSVC 14.52 or later")
  endif()
  # CMake maps MSVC C++23 to the newest draft dialect. Select ISO C++23 explicitly.
  set(CMAKE_CXX23_STANDARD_COMPILE_OPTION -std:c++23)
  set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION -std:c++23)
endif()
include(cmake/Dependencies.cmake)

function(hi_harden target)
  target_link_libraries(${target} PRIVATE Lorica::boundary)
  lorica_harden_target(${target})
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
endfunction()

add_library(hi_sqlite STATIC "${sqlite_SOURCE_DIR}/deps/sqlite3/sqlite3.c")
target_include_directories(hi_sqlite SYSTEM PUBLIC "${sqlite_SOURCE_DIR}/deps/sqlite3")
target_compile_definitions(hi_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
hi_harden(hi_sqlite)
if(NOT WIN32)
  target_link_libraries(hi_sqlite PRIVATE ${CMAKE_DL_LIBS} m)
endif()

add_library(hi_core STATIC
  src/api.cpp src/claude.cpp src/conversation.cpp src/database.cpp src/decision.cpp
  src/feed.cpp src/import.cpp src/input.cpp src/learning.cpp src/patterns.cpp
  src/profile.cpp src/questionnaire_api.cpp src/questionnaire_data.cpp src/questionnaire_followups.cpp
  src/questionnaire_profile.cpp src/questionnaire_prompts.cpp src/questionnaire_score.cpp
  src/render.cpp src/roles.cpp src/rules.cpp src/stance.cpp src/store.cpp src/store_learning.cpp src/store_questionnaire.cpp)
target_include_directories(hi_core PUBLIC src PRIVATE include)
target_link_libraries(hi_core PUBLIC Lapilli::net PRIVATE Lapilli::crypto Lapilli::one_shot hi_sqlite)
hi_harden(hi_core)
lorica_deprecate_unsafe_apis(hi_core)
if(MSVC)
  # Augur's synced runtime still uses u8path for compatibility with C++17.
  target_compile_definitions(hi_core PRIVATE _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
endif()

add_executable(histrio-server src/main.cpp)
target_link_libraries(histrio-server PRIVATE hi_core Lapilli::core)
hi_harden(histrio-server)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/excubitor.catalog.yaml")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/excubitor.catalog.yaml" port_line REGEX "^    port: [0-9]+$")
if(NOT port_line MATCHES "^    port: ([0-9]+)$")
  message(FATAL_ERROR "Expected a single Histrio port in excubitor.catalog.yaml")
endif()
target_compile_definitions(histrio-server PRIVATE HI_CATALOG_PORT=${CMAKE_MATCH_1})
set_target_properties(histrio-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")

if(BUILD_TESTING)
  add_executable(histrio-tests tests/acceptance.cpp tests/bootstrap.cpp tests/personality.cpp tests/questionnaire.cpp)
  target_link_libraries(histrio-tests PRIVATE hi_core hi_sqlite)
  target_include_directories(histrio-tests PRIVATE include)
  hi_harden(histrio-tests)
  target_compile_definitions(histrio-tests PRIVATE HI_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  foreach(test IN ITEMS health personas import render rules feed bootstrap_contract
                        profile situation role observe learning
                        pattern stance corral constructive privacy audit versions
                        questionnaire_assignment questionnaire_replay questionnaire_scoring
                        questionnaire_invalid questionnaire_privacy questionnaire_followups)
    add_test(NAME hi.${test} COMMAND histrio-tests ${test})
    set_tests_properties(hi.${test} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endforeach()
endif()
