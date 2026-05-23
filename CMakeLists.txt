cmake_minimum_required(VERSION 3.10.0)
project(UtopianModLauncher VERSION 0.1.0 LANGUAGES C CXX)

set(LAUNCHER_ICON_ICO "${CMAKE_CURRENT_BINARY_DIR}/uicon.ico")
set(LAUNCHER_RC "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc")

add_custom_command(
    OUTPUT "${LAUNCHER_ICON_ICO}" "${LAUNCHER_RC}"
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_icon.ps1"
        -InputPng "${CMAKE_CURRENT_SOURCE_DIR}/uicon.png"
        -OutputIco "${LAUNCHER_ICON_ICO}"
        -OutputRc "${LAUNCHER_RC}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/uicon.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_icon.ps1"
    VERBATIM
)

set_source_files_properties("${LAUNCHER_RC}" PROPERTIES GENERATED TRUE)

add_executable(UtopianModLauncher
    src/main.cpp
    "${LAUNCHER_RC}"
)

target_compile_features(UtopianModLauncher PRIVATE cxx_std_17)
target_compile_definitions(UtopianModLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

set_target_properties(UtopianModLauncher PROPERTIES
    OUTPUT_NAME "GameModLauncher"
)

if(MSVC)
    target_compile_options(UtopianModLauncher PRIVATE /W4 /EHsc)
endif()

add_custom_command(
    TARGET UtopianModLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/GameModLauncher.ini"
        "$<TARGET_FILE_DIR:UtopianModLauncher>/GameModLauncher.ini")
