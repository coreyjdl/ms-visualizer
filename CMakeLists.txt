cmake_minimum_required(VERSION 3.24)
project(MSVisualizer VERSION 0.1.2 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
add_executable(audio-tests tests/audio_tests.cpp)
target_include_directories(audio-tests PRIVATE src)
add_test(NAME audio-response COMMAND audio-tests)
option(PI_VISUALIZER "Build the Raspberry Pi GLES/ALSA application" OFF)
if(NOT WIN32 AND NOT PI_VISUALIZER)
  return()
endif()

include(FetchContent)
find_package(SDL2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(WIN32)
  find_package(GLEW REQUIRED)
  find_package(glm CONFIG REQUIRED)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(ENABLE_GLES OFF CACHE BOOL "" FORCE)
  set(ENABLE_SYSTEM_GLM ON CACHE BOOL "" FORCE)
  set(ENABLE_SYSTEM_PROJECTM_EVAL OFF CACHE BOOL "" FORCE)
  set(ENABLE_PLAYLIST ON CACHE BOOL "" FORCE)
  set(ENABLE_SDL_UI OFF CACHE BOOL "" FORCE)
  set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(projectm
    URL https://github.com/projectM-visualizer/projectm/releases/download/v4.1.6/libprojectM-4.1.6.tar.gz
    URL_HASH SHA256=1b9e6d56c59fe24e5416da4d42e941a34c982811003e43ac88b5aca8afa52c87
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(projectm)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PI_DEPS REQUIRED IMPORTED_TARGET sdl2 alsa glesv2 projectM-4>=4.1 projectM-4-playlist)
  find_package(Threads REQUIRED)
endif()
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(imgui)
if(WIN32)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/dependencies")
  file(DOWNLOAD
    https://raw.githubusercontent.com/mackron/miniaudio/350784a9467a79d0fa65802132668e5afbcf3777/miniaudio.h
    "${CMAKE_BINARY_DIR}/dependencies/miniaudio.h"
    EXPECTED_HASH SHA256=9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410
    TLS_VERIFY ON STATUS MINIAUDIO_DOWNLOAD)
  list(GET MINIAUDIO_DOWNLOAD 0 MINIAUDIO_STATUS)
  if(NOT MINIAUDIO_STATUS EQUAL 0)
    message(FATAL_ERROR "miniaudio download failed: ${MINIAUDIO_DOWNLOAD}")
  endif()
endif()

add_library(ui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(ui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(ui PUBLIC SDL2::SDL2)
if(PI_VISUALIZER AND NOT WIN32)
  target_compile_definitions(ui PRIVATE IMGUI_IMPL_OPENGL_ES3)
  add_executable(Pi-Visualizer src/main.cpp src/capture_alsa.cpp)
  target_compile_definitions(Pi-Visualizer PRIVATE SDL_MAIN_HANDLED)
  target_compile_options(Pi-Visualizer PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(Pi-Visualizer PRIVATE ui nlohmann_json::nlohmann_json PkgConfig::PI_DEPS Threads::Threads)
  add_custom_command(TARGET Pi-Visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/presets"
      "$<TARGET_FILE_DIR:Pi-Visualizer>/presets")
  return()
endif()
add_executable(MS-Visualizer WIN32 src/main.cpp src/capture.cpp)
target_compile_definitions(MS-Visualizer PRIVATE SDL_MAIN_HANDLED NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(MS-Visualizer PRIVATE src "${CMAKE_BINARY_DIR}/dependencies"
  "${projectm_SOURCE_DIR}/src/api/include"
  "${projectm_BINARY_DIR}/src/api/include"
  "${projectm_SOURCE_DIR}/src/playlist/api"
  "${projectm_BINARY_DIR}/src/playlist/api")
target_link_libraries(MS-Visualizer PRIVATE ui SDL2::SDL2 GLEW::GLEW
  nlohmann_json::nlohmann_json projectM projectM_playlist
  ole32 shell32 winmm uuid)
if(MSVC)
  target_compile_options(MS-Visualizer PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_command(TARGET MS-Visualizer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:MS-Visualizer>
    $<TARGET_FILE_DIR:MS-Visualizer> COMMAND_EXPAND_LISTS
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/presets" "$<TARGET_FILE_DIR:MS-Visualizer>/presets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
    "$<TARGET_FILE_DIR:MS-Visualizer>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${projectm_SOURCE_DIR}/LICENSE.txt"
    "$<TARGET_FILE_DIR:MS-Visualizer>/projectM-LICENSE.txt")
