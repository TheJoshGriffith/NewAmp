# Spotify source plugin. Like Plex, only this target links Qt Network.
find_package(Qt6 6.4 REQUIRED COMPONENTS Network)

# Builds can carry a default client ID; users can still set their own,
# which they need to once the app has five users (development mode).
set(NEWAMP_SPOTIFY_CLIENT_ID "" CACHE STRING "Default Spotify app client ID for the Spotify plugin")

# qt_add_plugin(name SHARED sources) drops the sources on Qt 6.4.
qt_add_plugin(newamp_spotify SHARED)
target_sources(newamp_spotify PRIVATE
    spotify.json
    SpotifyApi.h SpotifyApi.cpp
    SpotifyClient.h SpotifyClient.cpp
    SpotifyConnect.h SpotifyConnect.cpp
    SpotifyLibrary.h SpotifyLibrary.cpp
    SpotifyPlugin.h SpotifyPlugin.cpp
    SpotifyWebPlayer.h SpotifyWebPlayer.cpp
    SpotifyWebView.h
)
# NewAmp's own player runs Spotify's Web Playback SDK in the system web
# view, which only macOS has a suitable one of so far.
if(APPLE)
    target_sources(newamp_spotify PRIVATE SpotifyWebView_mac.mm)
    set_source_files_properties(SpotifyWebView_mac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(newamp_spotify PRIVATE "-framework WebKit" "-framework AppKit")
else()
    target_sources(newamp_spotify PRIVATE SpotifyWebView_none.cpp)
endif()
target_include_directories(newamp_spotify PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(newamp_spotify PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
target_compile_definitions(newamp_spotify PRIVATE
    QT_NO_KEYWORDS
    NEWAMP_VERSION="${PROJECT_VERSION}"
    NEWAMP_SPOTIFY_CLIENT_ID="${NEWAMP_SPOTIFY_CLIENT_ID}"
)

# Copied next to the app like the Plex plugin; see plex/CMakeLists.txt.
if(APPLE)
    set(newamp_plugin_dir "$<TARGET_BUNDLE_CONTENT_DIR:NewAmp>/PlugIns/newamp")
else()
    set(newamp_plugin_dir "$<TARGET_FILE_DIR:NewAmp>/plugins")
endif()
add_custom_command(TARGET newamp_spotify POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${newamp_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:newamp_spotify>" "${newamp_plugin_dir}/"
    VERBATIM
)
