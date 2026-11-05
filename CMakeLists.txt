cmake_minimum_required(VERSION 3.25)
project(CastlevaniaLauncher LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This launcher requires 64-bit Windows.")
endif()

add_executable(CastlevaniaLauncher WIN32 game_launcher.cpp)
target_compile_features(CastlevaniaLauncher PRIVATE cxx_std_20)
target_compile_definitions(CastlevaniaLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(CastlevaniaLauncher PRIVATE user32 shell32 comdlg32 gdi32)
set_property(TARGET CastlevaniaLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CTest)
if(BUILD_TESTING)
    add_executable(LauncherPathsTests launcher_paths_tests.cpp)
    target_compile_features(LauncherPathsTests PRIVATE cxx_std_20)
    add_test(NAME PortableLauncherPaths COMMAND LauncherPathsTests)
endif()
