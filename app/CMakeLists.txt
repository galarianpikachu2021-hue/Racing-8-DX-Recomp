find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(ZLIB REQUIRED)
qt_standard_project_setup()

qt_add_executable(kartrecomp
    src/main.cpp
    src/ini_file.cpp
    src/ini_file.h
    src/game_package.cpp
    src/game_package.h
    src/main_window.cpp
    src/updater.cpp
    src/updater.h
    src/main_window.h
    src/ctgp_convert.cpp
    src/ctgp_convert.h
    src/mod_addon.cpp
    src/text_merge.cpp
    src/text_merge.h
    src/mod_addon.h
    src/overlay_settings.cpp
    src/overlay_settings.h
    src/zip_archive.cpp
    src/zip_archive.h
)
target_link_libraries(kartrecomp PRIVATE Qt6::Widgets Qt6::Network ZLIB::ZLIB)
target_compile_definitions(kartrecomp PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(kartrecomp PROPERTIES WIN32_EXECUTABLE ON)
if (MSVC)
    target_compile_options(kartrecomp PRIVATE /W4 /utf-8)
else()
    target_compile_options(kartrecomp PRIVATE -Wall -Wextra)
endif()
