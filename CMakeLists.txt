cmake_minimum_required(VERSION 3.20)
project(SimpleTorrent VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find libtorrent via vcpkg
find_package(LibtorrentRasterbar CONFIG REQUIRED)

# Find OpenSSL (required by libtorrent)
find_package(OpenSSL REQUIRED)

# Collect sources
set(SOURCES
    src/main.cpp
    src/torrent_engine.cpp
)

# Add Windows executable (WIN32 = no console window)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} src/app.rc)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    LibtorrentRasterbar::torrent-rasterbar
    comctl32
    comdlg32
    shell32
    ole32
    uxtheme
)

# Enable Unicode + fix Windows header pollution
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Static runtime + desativa manifest automático para evitar conflito com app.rc
if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)
endif()