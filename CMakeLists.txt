cmake_minimum_required(VERSION 3.20)
project(Melovio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MELOVIO_BUILD_GUI   "Build Melovio Studio (needs SDL2 + OpenGL; fetched automatically if SDL2 is not installed)" ON)
option(MELOVIO_BUILD_CLI   "Build the melovio-cli command line tool" ON)
option(MELOVIO_BUILD_TESTS "Build the automated test suite" ON)
option(MELOVIO_WARNINGS_AS_ERRORS "Treat warnings in Melovio's own code as errors" OFF)
set(MELOVIO_WAUVIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wauvio-cpp" CACHE PATH
    "Directory containing wauvio.hpp / wauvio_vocal_v2.hpp (the Wauvio-CPP checkout). Treated as a read-only dependency.")

if(NOT EXISTS "${MELOVIO_WAUVIO_DIR}/wauvio_vocal_v2.hpp")
  message(FATAL_ERROR "wauvio_vocal_v2.hpp not found in MELOVIO_WAUVIO_DIR='${MELOVIO_WAUVIO_DIR}'")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
find_package(Threads REQUIRED)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW ON)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 3.0.2 GIT_SHALLOW ON)
FetchContent_MakeAvailable(nlohmann_json)
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(BUILD_FUZZERS OFF CACHE INTERNAL "")
set(AMALGAMATE_SOURCES OFF CACHE INTERNAL "")
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(miniz)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

function(melovio_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
    if(MELOVIO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(MELOVIO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(melovio_core STATIC
  src/core/util.cpp
  src/core/timeline.cpp
  src/core/project.cpp
  src/core/project_io.cpp
  src/core/history.cpp
  src/core/pronunciation.cpp
  src/core/voicebank.cpp
  src/core/voicebank_library.cpp
  src/core/synth_plan.cpp
  src/core/engine_bridge.cpp
  src/core/renderer.cpp
  src/core/wav_writer.cpp
  src/core/export.cpp
  src/core/transport.cpp
  src/core/config.cpp
  src/core/vb_validate.cpp
)
target_include_directories(melovio_core PUBLIC src)
# Wauvio is header-only; it is only included from engine_bridge.cpp, vb_validate.cpp and pronunciation.cpp.
# It is added as a SYSTEM include so its own (unmodified) code does not drown out Melovio warnings.
target_include_directories(melovio_core SYSTEM PRIVATE "${MELOVIO_WAUVIO_DIR}")
target_link_libraries(melovio_core PUBLIC nlohmann_json::nlohmann_json PRIVATE miniz Threads::Threads)
target_compile_definitions(melovio_core PUBLIC MELOVIO_VERSION="${PROJECT_VERSION}")
melovio_warnings(melovio_core)
set_source_files_properties(src/core/engine_bridge.cpp src/core/vb_validate.cpp src/core/pronunciation.cpp
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Default voicebanks shipped with Melovio are copied next to the binaries.
set(MELOVIO_BUNDLED_VOICEBANKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/voicebanks")
add_custom_target(melovio_voicebanks ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${MELOVIO_BUNDLED_VOICEBANKS_DIR}" "${CMAKE_BINARY_DIR}/voicebanks"
  COMMENT "Copying bundled voicebanks")

if(MELOVIO_BUILD_CLI)
  add_executable(melovio-cli src/cli/main.cpp)
  target_link_libraries(melovio-cli PRIVATE melovio_core)
  melovio_warnings(melovio-cli)
  add_dependencies(melovio-cli melovio_voicebanks)
endif()

if(MELOVIO_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(doctest)
  file(GLOB MELOVIO_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(melovio_tests ${MELOVIO_TEST_SOURCES})
  target_link_libraries(melovio_tests PRIVATE melovio_core miniz doctest::doctest)
  target_compile_definitions(melovio_tests PRIVATE
    MELOVIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MELOVIO_BUNDLED_VOICEBANKS="${MELOVIO_BUNDLED_VOICEBANKS_DIR}")
  melovio_warnings(melovio_tests)
  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  add_test(NAME melovio_tests COMMAND melovio_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(MELOVIO_BUILD_GUI)
  find_package(SDL2 QUIET)
  if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found on the system; fetching and building SDL 2.30.9 from source")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.30.9 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(SDL2)
    set(MELOVIO_SDL2_TARGET SDL2-static)
    set(MELOVIO_SDL2MAIN_TARGET SDL2main)
  else()
    if(TARGET SDL2::SDL2)
      set(MELOVIO_SDL2_TARGET SDL2::SDL2)
    else()
      set(MELOVIO_SDL2_TARGET SDL2)
    endif()
    if(TARGET SDL2::SDL2main)
      set(MELOVIO_SDL2MAIN_TARGET SDL2::SDL2main)
    endif()
  endif()
  # OpenGL needs no development package: SDL bundles its GL declarations and ImGui's OpenGL3 backend loads GL at run time.

  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5-docking GIT_SHALLOW ON)
  FetchContent_GetProperties(imgui)
  if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
  endif()
  add_library(imgui_sdl2_gl3 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui_sdl2_gl3 SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_sdl2_gl3 PUBLIC ${MELOVIO_SDL2_TARGET} ${CMAKE_DL_LIBS})

  add_library(melovio_audio_sdl STATIC src/audio/sdl_audio_output.cpp)
  target_link_libraries(melovio_audio_sdl PUBLIC melovio_core ${MELOVIO_SDL2_TARGET})
  melovio_warnings(melovio_audio_sdl)

  file(GLOB MELOVIO_UI_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
  add_executable(melovio-studio ${MELOVIO_UI_SOURCES})
  target_link_libraries(melovio-studio PRIVATE melovio_core melovio_audio_sdl imgui_sdl2_gl3
                        $<$<BOOL:${MELOVIO_SDL2MAIN_TARGET}>:${MELOVIO_SDL2MAIN_TARGET}>)
  melovio_warnings(melovio-studio)
  add_dependencies(melovio-studio melovio_voicebanks)

  if(MELOVIO_BUILD_TESTS)
    target_sources(melovio_tests PRIVATE tests/gui/sdl_audio_test.cpp)
    target_link_libraries(melovio_tests PRIVATE melovio_audio_sdl)
    target_compile_definitions(melovio_tests PRIVATE MELOVIO_HAVE_SDL_AUDIO=1)
  endif()
endif()

install(TARGETS melovio_core ARCHIVE DESTINATION lib)
if(MELOVIO_BUILD_CLI)
  install(TARGETS melovio-cli RUNTIME DESTINATION bin)
endif()
if(MELOVIO_BUILD_GUI)
  install(TARGETS melovio-studio RUNTIME DESTINATION bin)
endif()
install(DIRECTORY voicebanks/ DESTINATION share/melovio/voicebanks)
