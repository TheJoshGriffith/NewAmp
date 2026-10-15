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
