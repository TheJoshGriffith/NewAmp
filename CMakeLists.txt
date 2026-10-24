cmake_minimum_required(VERSION 3.21)

project(NewAmp VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX) # the Spotify plugin's web view
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEWAMP_BUILD_TESTS "Build unit tests" ON)
option(NEWAMP_BUILD_PLEX "Build the optional Plex source plugin" ON)
option(NEWAMP_BUILD_JELLYFIN "Build the optional Jellyfin source plugin" ON)
option(NEWAMP_BUILD_DLNA "Build the optional DLNA/UPnP media server plugin" ON)
option(NEWAMP_BUILD_SPOTIFY "Build the optional Spotify source plugin" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)
# zlib reads .wsz skins. Linux and macOS ship it; elsewhere (Windows) build
# it from source as a static library.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL ON) # only the static one is used
    target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# TagLib reads and writes tags for the File Info window. Built static with
# only the formats NewAmp plays and optimised for size: about 0.6 MB.
include(FetchContent)
FetchContent_Declare(taglib
    URL https://github.com/taglib/taglib/releases/download/v2.1.1/taglib-2.1.1.tar.gz
    URL_HASH SHA256=3716d31f7c83cbf17b67c8cf44dd82b2a2f17e6780472287a16823e70305ddba
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(newamp_saved_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(format APE DSF MOD SHORTEN TRUEAUDIO ZLIB)
    set(WITH_${format} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_MakeAvailable(taglib)
set(BUILD_SHARED_LIBS ${newamp_saved_shared_libs})
set_target_properties(tag PROPERTIES EXCLUDE_FROM_ALL ON POSITION_INDEPENDENT_CODE ON)
target_compile_options(tag PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O1,-Os> $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>)
# TagLib's headers sit in per-format folders and only its install tree puts
# them together, so use the directories it builds with.
get_target_property(taglib_include_dirs tag INCLUDE_DIRECTORIES)
list(APPEND taglib_include_dirs ${taglib_SOURCE_DIR}/taglib ${taglib_BINARY_DIR})

# Sources (and test data) contain UTF-8 text; MSVC otherwise assumes the
# system code page.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# miniaudio's decoders, compiled once as C with unused parts switched off.
add_library(newamp_miniaudio STATIC src/audio/MiniaudioImpl.c)
target_include_directories(newamp_miniaudio PUBLIC third_party/miniaudio)
set_target_properties(newamp_miniaudio PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(newamp_miniaudio PRIVATE -w)
endif()

# Everything except main() lives in a static library so tests can link it.
add_library(newamp_lib STATIC
    src/audio/AudioBackend.h
    src/audio/AudioDecoder.h src/audio/AudioDecoder.cpp
    src/audio/DspBackend.h src/audio/DspBackend.cpp
    src/audio/DspChain.h src/audio/DspChain.cpp
    src/audio/MediaPlayerBackend.h src/audio/MediaPlayerBackend.cpp
    src/audio/PcmSource.h
    src/audio/QtDecoderSource.h src/audio/QtDecoderSource.cpp
    src/audio/RemoteBackend.h src/audio/RemoteBackend.cpp
    src/audio/Spectrum.h src/audio/Spectrum.cpp
    src/audio/TagReader.h src/audio/TagReader.cpp
    src/core/Track.h
    src/core/MediaSource.h
    src/core/RemotePlayer.h
    src/core/LocalFileSource.h src/core/LocalFileSource.cpp
    src/core/LocalTagEditor.h src/core/LocalTagEditor.cpp
    src/core/MetadataEditor.h
    src/core/Playlist.h src/core/Playlist.cpp
    src/core/PlaybackEngine.h src/core/PlaybackEngine.cpp
    src/core/PlayerController.h src/core/PlayerController.cpp
    src/core/EqualizerSettings.h src/core/EqualizerSettings.cpp
    src/plugins/SourcePlugin.h
    src/plugins/PluginHost.h
    src/plugins/AppPluginHost.h src/plugins/AppPluginHost.cpp
    src/plugins/LibraryProvider.h
    src/plugins/PluginManager.h src/plugins/PluginManager.cpp
    src/skin/SkinSprites.h
    src/skin/Skin.h src/skin/Skin.cpp
    src/skin/DefaultSkin.h src/skin/DefaultSkin.cpp
    src/skin/ZipReader.h src/skin/ZipReader.cpp
    src/skin/SkinLoader.h src/skin/SkinLoader.cpp
    src/ui/SkinElement.h src/ui/SkinElement.cpp
    src/ui/SkinElements.h src/ui/SkinElements.cpp
    src/ui/SkinnedWindow.h src/ui/SkinnedWindow.cpp
    src/ui/SkinVisualizer.h src/ui/SkinVisualizer.cpp
    src/ui/WindowManager.h src/ui/WindowManager.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/EqualizerWindow.h src/ui/EqualizerWindow.cpp
    src/ui/FileInfoWindow.h src/ui/FileInfoWindow.cpp
    src/ui/GenericWindow.h src/ui/GenericWindow.cpp
    src/ui/SkinListView.h src/ui/SkinListView.cpp
    src/ui/LibraryWindow.h src/ui/LibraryWindow.cpp
    src/ui/PlaylistWindow.h src/ui/PlaylistWindow.cpp
    src/ui/VisualizerWindow.h src/ui/VisualizerWindow.cpp
    src/ui/vis/VisAnalyzer.h src/ui/vis/VisAnalyzer.cpp
    src/ui/vis/VisEffect.h src/ui/vis/VisEffect.cpp
    src/ui/vis/FeedbackEffect.cpp
    src/ui/vis/KaleidoscopeEffect.cpp
    src/ui/vis/TunnelEffect.cpp
    src/ui/vis/FireworksEffect.cpp
    src/ui/vis/MandelbrotEffect.cpp
    src/ui/vis/ClassicEffects.cpp
)
target_include_directories(newamp_lib PUBLIC src)
target_include_directories(newamp_lib SYSTEM PRIVATE ${taglib_include_dirs})
target_link_libraries(newamp_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Network PRIVATE ZLIB::ZLIB newamp_miniaudio tag)
target_compile_definitions(newamp_lib PUBLIC
    QT_NO_KEYWORDS
    NEWAMP_VERSION="${PROJECT_VERSION}"
)

qt_add_executable(NewAmp MACOSX_BUNDLE WIN32 src/main.cpp)
if(WIN32)
    target_sources(NewAmp PRIVATE packaging/newamp.rc)
endif()
target_link_libraries(NewAmp PRIVATE newamp_lib)
set_target_properties(NewAmp PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.newamp.NewAmp
    MACOSX_BUNDLE_BUNDLE_NAME NewAmp
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    # Our own template, for the local network prompt Plex needs on macOS 15+.
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
)

# Optional plugins are separate libraries; the app never links them.
if(NEWAMP_BUILD_PLEX)
    add_subdirectory(src/plugins/plex)
endif()
if(NEWAMP_BUILD_JELLYFIN)
    add_subdirectory(src/plugins/jellyfin)
endif()
if(NEWAMP_BUILD_DLNA)
    add_subdirectory(src/plugins/dlna)
endif()
if(NEWAMP_BUILD_SPOTIFY)
    add_subdirectory(src/plugins/spotify)
endif()

if(NEWAMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Renders the README screenshots headless; not part of the app.
option(NEWAMP_BUILD_SCREENSHOTS "Build the README screenshot tool" OFF)
if(NEWAMP_BUILD_SCREENSHOTS)
    qt_add_executable(newamp_screenshots tools/screenshots.cpp)
    target_link_libraries(newamp_screenshots PRIVATE newamp_lib)
endif()
