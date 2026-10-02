cmake_minimum_required(VERSION 3.15)
project(VGAFontEditor VERSION 1.0.0 LANGUAGES C RC)

# Static C runtime: a single .exe without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CORE_SOURCES src/font.c src/undo.c src/cp437.c)

add_executable(vgafontedit WIN32
    ${CORE_SOURCES}
    src/charmap.c
    src/dialogs.c
    src/glyphedit.c
    src/main.c
    src/preview.c
    src/render.c
    src/sysfont.c
    res/app.rc)

target_include_directories(vgafontedit PRIVATE src res)
target_compile_definitions(vgafontedit PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0501 WINVER=0x0501)
target_link_libraries(vgafontedit PRIVATE comctl32 comdlg32 shell32 gdi32 user32)

if(MSVC)
    target_compile_definitions(vgafontedit PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(vgafontedit PRIVATE /W4)
    # The manifest is embedded by app.rc.
    target_link_options(vgafontedit PRIVATE /MANIFEST:NO)
else()
    target_compile_options(vgafontedit PRIVATE -Wall -Wextra)
    target_link_options(vgafontedit PRIVATE -static)
endif()

# Unit tests for the platform independent core.
enable_testing()
add_executable(test_core tests/test_core.c ${CORE_SOURCES})
target_include_directories(test_core PRIVATE src)
if(MSVC)
    target_compile_definitions(test_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_core COMMAND test_core)
