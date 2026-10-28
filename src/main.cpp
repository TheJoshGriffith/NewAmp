#include "core/EqualizerSettings.h"
#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "plugins/AppPluginHost.h"
#include "plugins/PluginManager.h"
#include "ui/EqualizerWindow.h"
#include "ui/LibraryWindow.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistWindow.h"
#include "ui/VisualizerWindow.h"
#include "ui/WindowManager.h"

#include <QApplication>
#include <QSettings>

using namespace newamp;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NewAmp"));
    QApplication::setOrganizationName(QStringLiteral("NewAmp"));
    QApplication::setApplicationVersion(QStringLiteral(NEWAMP_VERSION));

    // Declared first so plugin libraries outlive everything that uses them.
    PluginManager plugins;
    LocalFileSource localSource;
    PlaybackEngine engine;
    engine.registerSource(&localSource);

    // Optional sources (Plex, Spotify, ...) load only when enabled in settings.
    // The host comes before the controller so it sees each end of track first.
    QSettings settings;
    Playlist playlist;
    AppPluginHost pluginHost(playlist, engine);
    plugins.discover();
    const QStringList enabled = settings.value(QStringLiteral("plugins/enabled")).toStringList();
    for (MediaSource *source : plugins.loadEnabled(QSet<QString>(enabled.begin(), enabled.end()), &pluginHost))
        engine.registerSource(source);

    PlayerController controller(playlist, engine);
    pluginHost.setController(&controller);
    EqualizerSettings equalizer;
    equalizer.load(settings);
    engine.setEqualizer(&equalizer);
    // Auto-load presets follow the playing track.
    QObject::connect(&playlist, &Playlist::currentIndexChanged, &equalizer, [&] {
        const Track *track = playlist.currentTrack();
        equalizer.setCurrentTrack(track ? track->location.toString() : QString());
    });
    engine.setBalance(settings.value(QStringLiteral("player/balance"), 0.0).toDouble());
    engine.setVolume(settings.value(QStringLiteral("player/volume"), 0.8).toFloat());
    playlist.setShuffle(settings.value(QStringLiteral("player/shuffle"), false).toBool());
    playlist.setRepeat(settings.value(QStringLiteral("player/repeat"), false).toBool());

    WindowManager windows;
    windows.setController(&controller);
    MainWindow mainWindow(&windows.skin(), controller);
    EqualizerWindow equalizerWindow(&windows.skin(), equalizer, &mainWindow);
    PlaylistWindow playlistWindow(&windows.skin(), controller, &mainWindow);
    LibraryWindow libraryWindow(&windows.skin(), pluginHost, &mainWindow);
    windows.addWindow(&mainWindow); // first: docked windows follow it
    windows.addWindow(&equalizerWindow);
    windows.addWindow(&playlistWindow);
    windows.addWindow(&libraryWindow, false);
    mainWindow.setCompanionWindows(&equalizerWindow, &playlistWindow);
    mainWindow.addWindowToggle(QObject::tr("Media Library"), &libraryWindow, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));

    // Plugins add their libraries during loading, before the window exists.
    for (LibraryProvider *library : pluginHost.libraries())
        libraryWindow.addLibrary(library);
    pluginHost.onLibraryAdded = [&](LibraryProvider *library) { libraryWindow.addLibrary(library); };
    pluginHost.onLibraryChanged = [&](LibraryProvider *library) { libraryWindow.reload(library); };
    pluginHost.onLibraryStatusChanged = [&](LibraryProvider *) { libraryWindow.update(); };
    pluginHost.onShowLibrary = [&](LibraryProvider *library) { libraryWindow.showLibrary(library); };

    // Full-window visualizations; they animate only while shown and playing.
    VisualizerWindow visualizerWindow(
        &windows.skin(), [&engine](float *out, int count) { return engine.visualizerSamples(out, count); }, &mainWindow);
    windows.addWindow(&visualizerWindow, false);
    mainWindow.addWindowToggle(QObject::tr("Visualization"), &visualizerWindow, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    QObject::connect(&engine, &PlaybackEngine::stateChanged, &visualizerWindow, [&](PlaybackEngine::State state) {
        visualizerWindow.setPlaying(state == PlaybackEngine::State::Playing);
    });
    QObject::connect(&playlist, &Playlist::currentIndexChanged, &visualizerWindow, &VisualizerWindow::trackChanged);

    mainWindow.setPluginManager(&plugins);
    pluginHost.setDialogParent(&mainWindow);
    windows.restoreLayout(settings);

    const QStringList args = app.arguments().mid(1);
    if (!args.isEmpty())
        controller.openTracks(LocalFileSource::tracksForPaths(args));

    QObject::connect(&app, &QApplication::aboutToQuit, [&] {
        windows.saveLayout(settings);
        equalizer.save(settings);
        settings.setValue(QStringLiteral("player/volume"), engine.volume());
        settings.setValue(QStringLiteral("player/balance"), engine.balance());
        settings.setValue(QStringLiteral("player/shuffle"), playlist.shuffle());
        settings.setValue(QStringLiteral("player/repeat"), playlist.repeat());
    });

    return app.exec();
}
