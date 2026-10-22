// Renders the README screenshots headless, from NewAmp's own built-in
// themes only. Run it with QT_QPA_PLATFORM=offscreen; it writes PNGs into
// the directory given as its first argument (default: docs/screenshots).

#include "core/EqualizerSettings.h"
#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "plugins/AppPluginHost.h"
#include "plugins/LibraryProvider.h"
#include "skin/DefaultSkin.h"
#include "ui/EqualizerWindow.h"
#include "ui/LibraryWindow.h"
#include "ui/MainWindow.h"
#include "ui/PlaylistWindow.h"
#include "ui/VisualizerWindow.h"
#include "ui/WindowManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QThread>
#include <cmath>

using namespace newamp;

namespace {

constexpr qreal Scale = 2.0;
const QColor Desktop(0x1d, 0x20, 0x26);

// Made-up artists, so the pictures promise nothing about anyone's music.
const QList<QPair<QString, QString>> Songs = {
    {QStringLiteral("Night Signal"), QStringLiteral("Glass Harbour")},
    {QStringLiteral("Paper Satellites"), QStringLiteral("Low Orbit")},
    {QStringLiteral("The Quiet Fields"), QStringLiteral("Amber Static")},
    {QStringLiteral("Night Signal"), QStringLiteral("Tidewater")},
    {QStringLiteral("Marlow Vance"), QStringLiteral("Slow Neon")},
    {QStringLiteral("Paper Satellites"), QStringLiteral("Weather Balloon")},
    {QStringLiteral("Cinder & Pine"), QStringLiteral("Kettle Song")},
};

// A few seconds of a chord with a kick drum, so the meters have something to show.
float sampleAt(double t)
{
    const double beat = std::fmod(t, 0.5);
    const double kick = std::exp(-beat * 18) * std::sin(2 * M_PI * (55 + 120 * std::exp(-beat * 30)) * beat);
    double chord = 0;
    for (double f : {220.0, 277.18, 329.63, 440.0, 1318.5, 2637.0})
        chord += std::sin(2 * M_PI * f * t + f) / (1 + f / 400);
    const double hat = std::fmod(t + 0.25, 0.5) < 0.03 ? (std::rand() / double(RAND_MAX) - 0.5) : 0;
    return float(0.6 * kick + 0.25 * chord * (0.6 + 0.4 * std::sin(t * 1.3)) + 0.3 * hat);
}

QString writeWav(const QString &path, double seconds)
{
    const int rate = 44100;
    const int frames = int(seconds * rate);
    QByteArray pcm(frames * 4, Qt::Uninitialized);
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < frames; ++i) {
        const auto s = qint16(qBound(-1.0f, sampleAt(double(i) / rate), 1.0f) * 30000);
        out[2 * i] = out[2 * i + 1] = s;
    }
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    auto u32 = [&](quint32 v) { file.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { file.write(reinterpret_cast<const char *>(&v), 2); };
    file.write("RIFF");
    u32(36 + pcm.size());
    file.write("WAVEfmt ");
    u32(16), u16(1), u16(2), u32(rate), u32(rate * 4), u16(4), u16(16);
    file.write("data");
    u32(pcm.size());
    file.write(pcm);
    return path;
}

void wait(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

class DemoLibrary final : public LibraryProvider
{
public:
    QString libraryName() const override { return QStringLiteral("Plex"); }
    void listItems(const QString &, const ItemsCallback &done) override
    {
        QList<LibraryItem> items;
        for (const char *name : {"Amber Static", "Cinder & Pine", "Glass Harbour", "Hollow Coast", "Juniper Lane",
                                 "Marlow Vance", "Night Signal", "Paper Satellites", "Static Bloom",
                                 "The Quiet Fields", "Velvet Arcade", "Winter Relay"})
            items.append({QString::fromLatin1(name), QString::fromLatin1(name), {}, true, {}});
        done(items);
    }
    void folderTracks(const QString &, const TracksCallback &done) override { done({}); }
    QString status() const override { return {}; }
};

// Draws windows onto one image at their places relative to `origin`.
QImage compose(const QList<QWidget *> &windows, int margin)
{
    QRect bounds;
    for (QWidget *w : windows)
        bounds |= w->geometry();
    QImage image(bounds.size() + QSize(2 * margin, 2 * margin), QImage::Format_RGB32);
    image.fill(Desktop);
    QPainter painter(&image);
    for (QWidget *w : windows)
        painter.drawPixmap(w->pos() - bounds.topLeft() + QPoint(margin, margin), w->grab());
    return image;
}

QImage grid(const QList<QImage> &tiles, int columns, int gap)
{
    const QSize tile = tiles.first().size();
    const int rows = (int(tiles.size()) + columns - 1) / columns;
    QImage image(columns * tile.width() + (columns + 1) * gap, rows * tile.height() + (rows + 1) * gap,
                 QImage::Format_RGB32);
    image.fill(Desktop);
    QPainter painter(&image);
    for (int i = 0; i < tiles.size(); ++i)
        painter.drawImage(gap + (i % columns) * (tile.width() + gap), gap + (i / columns) * (tile.height() + gap),
                          tiles[i]);
    return image;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QDir outDir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("docs/screenshots"));
    QDir().mkpath(outDir.path());
    const QString wav = writeWav(QDir::temp().filePath(QStringLiteral("newamp-demo.wav")), 240);

    LocalFileSource localSource;
    PlaybackEngine engine;
    engine.registerSource(&localSource);
    Playlist playlist;
    AppPluginHost pluginHost(playlist, engine);
    PlayerController controller(playlist, engine);
    pluginHost.setController(&controller);
    EqualizerSettings equalizer;
    for (int band = 0; band < EqualizerSettings::BandCount; ++band)
        equalizer.setBand(band, 7 * std::sin(band * 0.8 + 0.5));
    equalizer.setEnabled(true);
    engine.setEqualizer(&equalizer);
    engine.setVolume(0.6f);

    WindowManager windows;
    windows.setController(&controller);
    MainWindow mainWindow(&windows.skin(), controller);
    EqualizerWindow equalizerWindow(&windows.skin(), equalizer, &mainWindow);
    PlaylistWindow playlistWindow(&windows.skin(), controller, &mainWindow);
    LibraryWindow libraryWindow(&windows.skin(), pluginHost, &mainWindow);
    VisualizerWindow visualizerWindow(
        &windows.skin(), [](float *out, int count) {
            static double t = 0;
            for (int i = 0; i < count; ++i)
                out[i] = sampleAt(t + i / 44100.0);
            t += 1 / 30.0;
            return count;
        },
        &mainWindow);
    windows.addWindow(&mainWindow);
    windows.addWindow(&equalizerWindow);
    windows.addWindow(&playlistWindow);
    windows.addWindow(&libraryWindow, false);
    windows.addWindow(&visualizerWindow, false);
    mainWindow.setCompanionWindows(&equalizerWindow, &playlistWindow);
    DemoLibrary library;
    libraryWindow.addLibrary(&library);
    libraryWindow.showLibrary(&library);
    windows.setScale(Scale);

    QList<Track> tracks;
    for (const auto &[artist, title] : Songs) {
        Track track;
        track.sourceId = localSource.id();
        track.location = QUrl::fromLocalFile(wav);
        track.artist = artist;
        track.title = title;
        track.durationMs = 180000 + 37000 * qint64(tracks.size() % 4);
        tracks.append(track);
    }
    controller.openTracks(tracks);
    wait(1000);

    // Restart the track just before each picture, so its title reads from the start.
    auto settle = [&] {
        controller.playIndex(1);
        wait(100);
        controller.playIndex(0);
        wait(300);
        engine.seek(83000);
        wait(300);
    };

    // Everything docked: the classic stack with the library and visualizer beside it.
    windows.defaultLayout();
    for (SkinnedWindow *w : windows.windows())
        w->show();
    playlistWindow.setSkinSize({275, 116 + 3 * 29});
    libraryWindow.setSkinSize({275 + 4 * 25, 232});
    libraryWindow.move(mainWindow.geometry().topRight() + QPoint(1, 0));
    visualizerWindow.setSkinSize({275 + 4 * 25, 203});
    visualizerWindow.move(libraryWindow.geometry().bottomLeft() + QPoint(0, 1));
    visualizerWindow.setPlaying(true);
    visualizerWindow.setEffect(2);
    for (int i = 0; i < 90; ++i) {
        visualizerWindow.tick();
        wait(33);
    }
    settle();
    const QList<QWidget *> all{&mainWindow, &equalizerWindow, &playlistWindow, &libraryWindow, &visualizerWindow};
    compose(all, 24).save(outDir.filePath(QStringLiteral("newamp.png")));

    // The main window in each built-in theme.
    libraryWindow.hide();
    visualizerWindow.hide();
    QList<QImage> themes;
    for (const DefaultSkin::ThemeInfo &theme : DefaultSkin::themes()) {
        windows.loadSkin(WindowManager::themePath(theme.id));
        settle();
        themes.append(compose({&mainWindow, &equalizerWindow, &playlistWindow}, 0));
    }
    grid(themes, 4, 16).save(outDir.filePath(QStringLiteral("themes.png")));

    // Every visualizer effect.
    windows.useDefaultSkin();
    visualizerWindow.show();
    visualizerWindow.setSkinSize(VisualizerWindow::DefaultSize);
    QList<QImage> effects;
    for (int e = 0; e < visualizerWindow.effectCount(); ++e) {
        visualizerWindow.setEffect(e);
        for (int i = 0; i < 120; ++i) {
            visualizerWindow.tick();
            wait(33);
        }
        effects.append(visualizerWindow.grab().toImage());
    }
    grid(effects, 4, 16).save(outDir.filePath(QStringLiteral("visualizations.png")));

    controller.stop();
    QFile::remove(wav);
    return 0;
}
