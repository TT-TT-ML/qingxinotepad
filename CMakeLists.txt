cmake_minimum_required(VERSION 3.16)
project(qingxinotepad VERSION 1.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use static runtime for MSVC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LEGACY_NOTEPAD_WERROR "Treat warnings as errors" OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup")
else()
    add_compile_options(-Wall -Wextra -municode -Wno-error=missing-field-initializers)
    if(LEGACY_NOTEPAD_WERROR)
        add_compile_options(-Werror)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -mwindows")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
    endif()
endif()

add_executable(qingxinotepad WIN32
    src/main.cpp
    src/core/globals.cpp
    src/core/storage.cpp
    src/lang/lang.cpp
    src/modules/theme.cpp
    src/modules/editor.cpp
    src/modules/file.cpp
    src/modules/ui.cpp
    src/modules/background.cpp
    src/modules/dialog.cpp
    src/modules/commands.cpp
    src/modules/menu.cpp
    src/modules/menu_skin.cpp
    src/modules/settings.cpp
    src/modules/syntax.cpp
    src/modules/codeeditor.cpp
    src/modules/compiler.cpp
    src/modules/compilewindow.cpp
    src/modules/tabs.cpp
    src/modules/netlib.cpp
    src/modules/libsource.cpp
    src/modules/libcatalog.cpp
    src/modules/libwindow.cpp
    src/modules/presets.cpp
    src/modules/symbols.cpp
    src/notepad.rc
)

target_include_directories(qingxinotepad PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/modules)
target_compile_definitions(qingxinotepad PRIVATE UNICODE _UNICODE)

target_link_libraries(qingxinotepad PRIVATE
    comctl32
    shlwapi
    comdlg32
    shell32
    gdiplus
    msimg32
    user32
    gdi32
    kernel32
    dwmapi
    uxtheme
    imm32
    winhttp
)

if(NOT MSVC)
    target_link_options(qingxinotepad PRIVATE -mwindows -municode)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(qingxinotepad PRIVATE -static)
        target_link_libraries(qingxinotepad PRIVATE -static-libgcc -static-libstdc++)
    endif()
    if(CMAKE_STRIP)
        add_custom_command(TARGET qingxinotepad POST_BUILD COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:qingxinotepad>)
    endif()
endif()

# Installation
install(TARGETS qingxinotepad DESTINATION .)
install(FILES LICENSE README.md DESTINATION .)

# Installer / packaging (CPack with NSIS)
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "qingxinotepad")
set(CPACK_PACKAGE_VENDOR "Qingxin Notepad")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(CMAKE_GENERATOR_PLATFORM)
    set(CPACK_PACKAGE_FILE_NAME "qingxinotepad-${PROJECT_VERSION}-${CMAKE_GENERATOR_PLATFORM}")
else()
    set(CPACK_PACKAGE_FILE_NAME "qingxinotepad-${PROJECT_VERSION}")
endif()
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_NSIS_DISPLAY_NAME "Qingxin Notepad")
set(CPACK_NSIS_PACKAGE_NAME "Qingxin Notepad")
# CPACK_NSIS_URL_INFO_ABOUT intentionally unset: no public project URL yet
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/icon.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/icon.ico")

include(CPack)
