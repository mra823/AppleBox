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

# --- Vendored MAME m6502 core (BSD-3-Clause; see THIRDPARTY.md) ---
# Opcode dispatch tables are generated at build time by MAME's own
# m6502make.py from the vendored .lst files.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(M6502_VENDOR ${CMAKE_SOURCE_DIR}/src/cpu/m6502/vendor)
set(M6502_GEN ${CMAKE_BINARY_DIR}/generated/cpu/m6502)
file(MAKE_DIRECTORY ${M6502_GEN})
add_custom_command(
  OUTPUT ${M6502_GEN}/m6502.hxx
  COMMAND Python3::Interpreter ${M6502_VENDOR}/m6502make.py s m6502
          ${M6502_VENDOR}/om6502.lst ${M6502_VENDOR}/dm6502.lst
          ${M6502_GEN}/m6502.hxx
  DEPENDS ${M6502_VENDOR}/m6502make.py ${M6502_VENDOR}/om6502.lst
          ${M6502_VENDOR}/dm6502.lst
  COMMENT "Generating m6502.hxx (MAME m6502make.py)"
)
add_custom_command(
  OUTPUT ${M6502_GEN}/m6502d.hxx
  COMMAND Python3::Interpreter ${M6502_VENDOR}/m6502make.py d m6502
          - ${M6502_VENDOR}/dm6502.lst ${M6502_GEN}/m6502d.hxx
  DEPENDS ${M6502_VENDOR}/m6502make.py ${M6502_VENDOR}/dm6502.lst
  COMMENT "Generating m6502d.hxx (MAME m6502make.py)"
)
add_library(ab_m6502 STATIC
  ${M6502_VENDOR}/m6502.cpp
  ${M6502_VENDOR}/m6502d.cpp
  src/cpu/m6502/m6502_core.cpp
  ${M6502_GEN}/m6502.hxx
  ${M6502_GEN}/m6502d.hxx
)
set_source_files_properties(${M6502_GEN}/m6502.hxx ${M6502_GEN}/m6502d.hxx
  PROPERTIES HEADER_FILE_ONLY ON GENERATED ON)
target_include_directories(ab_m6502
  PUBLIC src
  PRIVATE src/cpu/mame_shim ${M6502_VENDOR} ${CMAKE_BINARY_DIR}/generated)
# Vendored code is compiled without our warning set.

# --- Core emulator library (no UI dependencies) ---
add_library(applebox_core STATIC
  src/core/scheduler.cpp
  src/core/savestate.cpp
  src/core/rom_manifest.cpp
  src/core/sha256.cpp
  src/core/machine.cpp
  src/devices/pia6821.cpp
  src/machines/apple1.cpp
)
target_include_directories(applebox_core PUBLIC src)
target_compile_options(applebox_core PRIVATE -Wall -Wextra)
target_link_libraries(applebox_core PUBLIC ab_m6502)

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
  foreach(t scheduler savestate rom_manifest apple1 harte_6502 klaus_6502)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE applebox_core)
    add_test(NAME ${t} COMMAND test_${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  set_tests_properties(harte_6502 PROPERTIES TIMEOUT 3600)
  set_tests_properties(klaus_6502 PROPERTIES TIMEOUT 600)
endif()
