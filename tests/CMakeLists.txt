find_package(Qt6 REQUIRED COMPONENTS Test)

function(newamp_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE newamp_lib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Tests that touch QPixmap need a GUI app; run them headless.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

newamp_add_test(tst_playlist)
newamp_add_test(tst_skin)
newamp_add_test(tst_skinloader)
target_link_libraries(tst_skinloader PRIVATE ZLIB::ZLIB)
newamp_add_test(tst_windows)
newamp_add_test(tst_library)
newamp_add_test(tst_audio)
newamp_add_test(tst_visualizer)

# A minimal source plugin, built into its own directory for tst_plugins.
qt_add_plugin(newamp_dummyplugin SHARED)
target_sources(newamp_dummyplugin PRIVATE dummyplugin/DummyPlugin.cpp)
target_include_directories(newamp_dummyplugin PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(newamp_dummyplugin PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(newamp_dummyplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins)

newamp_add_test(tst_plugins)
add_dependencies(tst_plugins newamp_dummyplugin)
target_compile_definitions(tst_plugins PRIVATE
    NEWAMP_TEST_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")

# Plex parsing and reporting, tested from fixtures without a network.
if(NEWAMP_BUILD_PLEX)
    set(plex_dir ${PROJECT_SOURCE_DIR}/src/plugins/plex)
    newamp_add_test(tst_plex)
    target_sources(tst_plex PRIVATE ${plex_dir}/PlexApi.cpp ${plex_dir}/PlexReporter.cpp)
    target_include_directories(tst_plex PRIVATE ${plex_dir})
    target_compile_definitions(tst_plex PRIVATE NEWAMP_PLEX_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/plex")
endif()

# Jellyfin parsing, reporting, and sign-in and browsing against a fake server.
if(NEWAMP_BUILD_JELLYFIN)
    set(jellyfin_dir ${PROJECT_SOURCE_DIR}/src/plugins/jellyfin)
    newamp_add_test(tst_jellyfin)
    target_sources(tst_jellyfin PRIVATE
        ${jellyfin_dir}/JellyfinApi.cpp ${jellyfin_dir}/JellyfinReporter.cpp
        ${jellyfin_dir}/JellyfinClient.h ${jellyfin_dir}/JellyfinClient.cpp
        ${jellyfin_dir}/JellyfinLibrary.h ${jellyfin_dir}/JellyfinLibrary.cpp)
    target_include_directories(tst_jellyfin PRIVATE ${jellyfin_dir})
    target_compile_definitions(tst_jellyfin PRIVATE NEWAMP_JELLYFIN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/jellyfin")
endif()

# DLNA discovery, descriptions, SOAP and DIDL-Lite, tested from fixtures.
if(NEWAMP_BUILD_DLNA)
    set(dlna_dir ${PROJECT_SOURCE_DIR}/src/plugins/dlna)
    newamp_add_test(tst_dlna)
    target_sources(tst_dlna PRIVATE ${dlna_dir}/DlnaApi.cpp)
    target_include_directories(tst_dlna PRIVATE ${dlna_dir})
    target_compile_definitions(tst_dlna PRIVATE NEWAMP_DLNA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/dlna")
endif()

# Spotify parsing and sign-in helpers from fixtures, and playback against a
# fake Web API.
if(NEWAMP_BUILD_SPOTIFY)
    set(spotify_dir ${PROJECT_SOURCE_DIR}/src/plugins/spotify)
    newamp_add_test(tst_spotify)
    target_sources(tst_spotify PRIVATE
        ${spotify_dir}/SpotifyApi.cpp
        ${spotify_dir}/SpotifyClient.h ${spotify_dir}/SpotifyClient.cpp
        ${spotify_dir}/SpotifyConnect.h ${spotify_dir}/SpotifyConnect.cpp)
    target_include_directories(tst_spotify PRIVATE ${spotify_dir})
    target_compile_definitions(tst_spotify PRIVATE NEWAMP_SPOTIFY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/spotify")
endif()
