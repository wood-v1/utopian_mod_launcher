cmake_minimum_required(VERSION 3.10.0)
project(UtopianModLauncher VERSION 1.0.0 LANGUAGES C CXX)

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
    src/dll_injector.cpp
    src/launcher_cli.cpp
    src/launcher_config.cpp
    src/launch_overlay.cpp
    src/launcher_runtime.cpp
    src/launcher_services.cpp
    src/load_order.cpp
    src/main.cpp
    src/mod_discovery.cpp
    src/mod_ini.cpp
    src/mod_package.cpp
    src/path_utils.cpp
    src/process_utils.cpp
    src/self_tests.cpp
    src/string_utils.cpp
    src/windows_ui/file_dialogs.cpp
    src/windows_ui/install_options_dialog.cpp
    src/windows_ui/install_source_dialog.cpp
    src/windows_ui/install_source_strategy.cpp
    src/windows_ui/launcher_help.cpp
    src/windows_ui/launcher_window.cpp
    src/windows_ui/mod_settings_dialog.cpp
    src/windows_ui/mod_list_view.cpp
    src/windows_ui/package_target_dialog.cpp
    src/windows_ui/ui_helpers.cpp
    "${LAUNCHER_RC}"
)

target_compile_features(UtopianModLauncher PRIVATE cxx_std_17)
target_compile_definitions(UtopianModLauncher PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=0x0501
    _WIN32_IE=0x0500)
target_link_libraries(UtopianModLauncher PRIVATE comctl32 comdlg32 shell32 ole32 oleaut32 bcrypt gdiplus)

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

add_custom_command(
    TARGET UtopianModLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:UtopianModLauncher>/mods/.launcher"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/.launcher/banner.txt"
        "$<TARGET_FILE_DIR:UtopianModLauncher>/mods/.launcher/banner.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mods/.launcher/banner.bmp"
        "$<TARGET_FILE_DIR:UtopianModLauncher>/mods/.launcher/banner.bmp")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mods/.launcher/banner.png")
    add_custom_command(
        TARGET UtopianModLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mods/.launcher/banner.png"
            "$<TARGET_FILE_DIR:UtopianModLauncher>/mods/.launcher/banner.png")
endif()
