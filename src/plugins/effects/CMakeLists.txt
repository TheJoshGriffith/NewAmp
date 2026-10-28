# Sound effects: a small DSP plugin with no dependencies beyond Qt.

# qt_add_plugin(name SHARED sources) drops the sources on Qt 6.4.
qt_add_plugin(newamp_effects SHARED)
target_sources(newamp_effects PRIVATE
    effects.json
    SoundEffects.h SoundEffects.cpp
    EffectsPlugin.cpp
)
target_include_directories(newamp_effects PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(newamp_effects PRIVATE Qt6::Core Qt6::Gui)
target_compile_definitions(newamp_effects PRIVATE
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
add_custom_command(TARGET newamp_effects POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${newamp_plugin_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:newamp_effects>" "${newamp_plugin_dir}/"
    VERBATIM
)
