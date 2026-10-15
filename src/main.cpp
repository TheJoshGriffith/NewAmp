#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "plugins/PluginManager.h"
#include "skin/DefaultSkin.h"
#include "ui/MainWindow.h"

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
    plugins.discover();
    const QStringList enabled = QSettings().value(QStringLiteral("plugins/enabled")).toStringList();
    for (MediaSource *source : plugins.loadEnabled(QSet<QString>(enabled.begin(), enabled.end())))
        engine.registerSource(source);

    Playlist playlist;
    PlayerController controller(playlist, engine);

    const Skin skin = DefaultSkin::create();
    MainWindow window(&skin, controller);
    window.show();

    const QStringList args = app.arguments().mid(1);
    if (!args.isEmpty())
        controller.openTracks(LocalFileSource::tracksForPaths(args));

    return app.exec();
}
