cmake_minimum_required(VERSION 3.20)
project(applebox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(APPLEBOX_BUILD_TESTS "Build unit tests" ON)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# --- Dear ImGui (docking branch, vendored submodule, MIT) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# --- Core emulator library (no UI dependencies) ---
add_library(applebox_core STATIC
  src/core/scheduler.cpp
  src/core/savestate.cpp
  src/core/rom_manifest.cpp
  src/core/sha256.cpp
  src/core/machine.cpp
)
target_include_directories(applebox_core PUBLIC src)
target_compile_options(applebox_core PRIVATE -Wall -Wextra)

# --- Main application ---
add_executable(applebox
  src/main.cpp
  src/ui/main_window.cpp
  src/ui/machine_config_dialog.cpp
)
target_link_libraries(applebox PRIVATE applebox_core imgui SDL2::SDL2 OpenGL::GL)
target_compile_options(applebox PRIVATE -Wall -Wextra)

# --- Tests ---
if(APPLEBOX_BUILD_TESTS)
  enable_testing()
  foreach(t scheduler savestate rom_manifest)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE applebox_core)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()
