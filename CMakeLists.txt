cmake_minimum_required(VERSION 3.21)

project(MoH2010.LazmFix VERSION 0.9.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# moh.exe is a 32-bit game.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "MoH2010.LazmFix targets Windows x86 (32-bit) with MSVC")
endif()

# Static CRT: nothing extra to install next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/version.rc @ONLY)
set(MOHPCF_VERSION_RC ${CMAKE_BINARY_DIR}/version.rc)

add_library(mohpcf_common STATIC
  src/common/file_util.cpp
  src/common/logger.cpp
  src/core/hid_guard.cpp
)
target_include_directories(mohpcf_common PUBLIC src)
target_compile_definitions(mohpcf_common PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mohpcf_common PRIVATE /W4 /permissive- /EHsc /utf-8)

# Core: dinput8.dll proxy, loaded by moh.exe from Binaries\.
add_library(MoH2010.LazmFix.Core MODULE
  src/core/core.cpp
  src/core/proxy.cpp
  src/core/window.cpp
  src/core/startup.cpp
  src/core/subtitles.cpp
  src/core/ads.cpp
  src/core/fov.cpp
  src/core/hud.cpp
  src/core/dinput8.def
  ${MOHPCF_VERSION_RC}
)
target_link_libraries(MoH2010.LazmFix.Core PRIVATE mohpcf_common user32 advapi32 version dxguid)
target_compile_definitions(MoH2010.LazmFix.Core PRIVATE MOHPCF_VERSION="${PROJECT_VERSION}")
target_compile_options(MoH2010.LazmFix.Core PRIVATE /W4 /permissive- /EHsc /utf-8)
set_target_properties(MoH2010.LazmFix.Core PROPERTIES PREFIX "" OUTPUT_NAME "dinput8" SUFFIX ".dll" PDB_NAME "MoH2010.LazmFix.Core")

# ReShade add-on front-end (32-bit ReShade loads *.addon32).
add_library(MoH2010.LazmFix.Addon MODULE
  src/addon/reshade_addon.cpp
  src/addon/display.cpp
  ${MOHPCF_VERSION_RC}
)
target_include_directories(MoH2010.LazmFix.Addon PRIVATE src third_party/reshade/include third_party/imgui)
target_compile_definitions(MoH2010.LazmFix.Addon PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(MoH2010.LazmFix.Addon PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(MoH2010.LazmFix.Addon PRIVATE user32 shell32 ole32)
set_target_properties(MoH2010.LazmFix.Addon PROPERTIES PREFIX "" OUTPUT_NAME "MoH2010.LazmFix" SUFFIX ".addon32"
  PDB_NAME "MoH2010.LazmFix.Addon")

add_executable(mohpcf_unit_tests tests/unit_tests.cpp)
target_link_libraries(mohpcf_unit_tests PRIVATE mohpcf_common)
target_compile_options(mohpcf_unit_tests PRIVATE /W4 /permissive- /EHsc /utf-8)

# The proxy decides by process name, so the test also runs as a copy named moh.exe.
add_executable(proxy_test tests/proxy_test.cpp)
target_link_libraries(proxy_test PRIVATE mohpcf_common user32 dxguid)
target_compile_options(proxy_test PRIVATE /W4 /permissive- /EHsc /utf-8)
add_dependencies(proxy_test MoH2010.LazmFix.Core)
add_custom_command(TARGET proxy_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:proxy_test> $<TARGET_FILE_DIR:proxy_test>/as_game/moh.exe
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:proxy_test> $<TARGET_FILE_DIR:proxy_test>/as_other/other.exe)

enable_testing()
add_test(NAME unit_tests COMMAND mohpcf_unit_tests)
add_test(NAME proxy_in_game_process
  COMMAND $<TARGET_FILE_DIR:proxy_test>/as_game/moh.exe $<TARGET_FILE:MoH2010.LazmFix.Core>)
add_test(NAME proxy_in_other_process
  COMMAND $<TARGET_FILE_DIR:proxy_test>/as_other/other.exe $<TARGET_FILE:MoH2010.LazmFix.Core> --not-game)
set_tests_properties(proxy_in_game_process proxy_in_other_process PROPERTIES RUN_SERIAL TRUE)

# NVIDIA driver profile tool: sets SGSSAA in the profile that owns moh.exe (never the global one).
add_executable(MoH2010.LazmFix.NvProfile src/tools/nv_profile.cpp ${MOHPCF_VERSION_RC})
target_include_directories(MoH2010.LazmFix.NvProfile PRIVATE third_party/nvapi)
target_compile_definitions(MoH2010.LazmFix.NvProfile PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(MoH2010.LazmFix.NvProfile PRIVATE /W3 /EHsc)
