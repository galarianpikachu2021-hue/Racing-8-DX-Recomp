find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# Qt6::Gui links OpenGL (libOpenGL + libGLX) into every program that uses it, though the launcher never calls OpenGL: drop it, so the
# AppImage starts on systems without libopengl0 (Qt itself loads OpenGL at run time when it needs it).
if(UNIX AND NOT APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "")
endif()
find_package(ZLIB REQUIRED)
qt_standard_project_setup()

# The build pipeline (unit splitting, compile scheduling), shared by the launcher and the tests.
add_library(kartrecomp_build STATIC src/unit_split.cpp src/unit_split.h src/build_runner.cpp src/build_runner.h
    src/nsp_info.cpp src/nsp_info.h src/keys_check.cpp src/keys_check.h src/setup_validate.cpp src/setup_validate.h src/setup_paths.cpp src/setup_paths.h src/zip_archive.cpp src/zip_archive.h src/verify_build.cpp src/verify_build.h src/game_package.cpp src/game_package.h src/ini_file.cpp src/ini_file.h src/setup_pipeline.cpp src/setup_pipeline.h src/mii_db.cpp src/mii_db.h src/menu_merge.cpp src/menu_merge.h)
find_package(ZLIB REQUIRED)
target_link_libraries(kartrecomp_build PUBLIC Qt6::Core ZLIB::ZLIB)
target_include_directories(kartrecomp_build PUBLIC src)

qt_add_executable(kartrecomp
    src/main.cpp
    src/ini_file.h
    src/game_package.h
    src/main_window.cpp
    src/controller_mapper.cpp
    src/controller_mapper.h
    src/updater.cpp
    src/updater.h
    src/main_window.h
    src/setup_page.cpp
    src/setup_page.h
    src/ctgp_convert.cpp
    src/ctgp_convert.h
    src/mod_addon.cpp
    src/mii_editor.cpp
    src/mii_editor.h
    src/text_merge.cpp
    src/text_merge.h
    src/mod_addon.h
    src/overlay_settings.cpp
    src/overlay_settings.h
    src/zip_archive.h
)
target_link_libraries(kartrecomp PRIVATE Qt6::Widgets Qt6::Network ZLIB::ZLIB kartrecomp_build)
target_compile_definitions(kartrecomp PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(kartrecomp PROPERTIES WIN32_EXECUTABLE ON)
if (MSVC)
    target_compile_options(kartrecomp PRIVATE /W4 /utf-8)
else()
    target_compile_options(kartrecomp PRIVATE -Wall -Wextra)
endif()

# Command-line front end of the build pipeline.
add_executable(kartrecomp-build src/build_cli.cpp)
target_link_libraries(kartrecomp-build PRIVATE kartrecomp_build)

# The Setup page's pipeline from a shell.
add_executable(kartrecomp-setup src/setup_cli.cpp)
target_link_libraries(kartrecomp-setup PRIVATE kartrecomp_build)
