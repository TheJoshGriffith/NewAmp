# Jellyfin source plugin.
find_package(Qt6 6.4 REQUIRED COMPONENTS Network)

# qt_add_plugin(name SHARED sources) drops the sources on Qt 6.4.
qt_add_plugin(newamp_jellyfin SHARED)
target_sources(newamp_jellyfin PRIVATE
    jellyfin.json
    JellyfinApi.h JellyfinApi.cpp
    JellyfinClient.h JellyfinClient.cpp
    JellyfinReporter.h JellyfinReporter.cpp
    JellyfinLibrary.h JellyfinLibrary.cpp
    JellyfinSignInDialog.h JellyfinSignInDialog.cpp
    JellyfinPlugin.h JellyfinPlugin.cpp
)
target_include_directories(newamp_jellyfin PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(newamp_jellyfin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
target_compile_definitions(newamp_jellyfin PRIVATE
    QT_NO_KEYWORDS
    NEWAMP_VERSION="${PROJECT_VERSION}"
)

# Copy the plugin to where PluginManager::defaultSearchPaths() looks: inside
# the app bundle on macOS (so macdeployqt and the .dmg pick it up), next to
# the executable elsewhere.
if(APPLE)
    set(newamp_plugin_dir "$<TARGET_BUNDLE_CONTENT_DIR:NewAmp>/PlugIns/newamp")
else()
    set(newamp_plugin_dir "$<TARGET_FILE_DIR:NewAmp>/plugins")
endif()
add_custom_command(TARGET newamp_jellyfin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${newamp_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:newamp_jellyfin>" "${newamp_plugin_dir}/"
    VERBATIM
)
