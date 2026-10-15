cmake_minimum_required(VERSION 3.21)

project(NewAmp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEWAMP_BUILD_TESTS "Build unit tests" ON)
option(NEWAMP_BUILD_PLEX "Build the optional Plex source plugin" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
find_package(ZLIB REQUIRED)

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
    src/audio/Spectrum.h src/audio/Spectrum.cpp
    src/audio/TagReader.h src/audio/TagReader.cpp
    src/core/Track.h
    src/core/MediaSource.h
    src/core/LocalFileSource.h src/core/LocalFileSource.cpp
    src/core/Playlist.h src/core/Playlist.cpp
    src/core/PlaybackEngine.h src/core/PlaybackEngine.cpp
    src/core/PlayerController.h src/core/PlayerController.cpp
    src/core/EqualizerSettings.h src/core/EqualizerSettings.cpp
    src/plugins/SourcePlugin.h
    src/plugins/PluginHost.h
    src/plugins/AppPluginHost.h src/plugins/AppPluginHost.cpp
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
    src/ui/PlaylistWindow.h src/ui/PlaylistWindow.cpp
)
target_include_directories(newamp_lib PUBLIC src)
target_link_libraries(newamp_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia PRIVATE ZLIB::ZLIB newamp_miniaudio)
target_compile_definitions(newamp_lib PUBLIC
    QT_NO_KEYWORDS
    NEWAMP_VERSION="${PROJECT_VERSION}"
)

qt_add_executable(NewAmp MACOSX_BUNDLE WIN32 src/main.cpp)
target_link_libraries(NewAmp PRIVATE newamp_lib)
set_target_properties(NewAmp PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.newamp.NewAmp
    MACOSX_BUNDLE_BUNDLE_NAME NewAmp
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

# Optional plugins are separate libraries; the app never links them.
if(NEWAMP_BUILD_PLEX)
    add_subdirectory(src/plugins/plex)
endif()

if(NEWAMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
