#include "core/MediaSource.h"
#include "plugins/DspEffect.h"
#include "plugins/PluginHost.h"
#include "plugins/PluginManager.h"
#include "plugins/SourcePlugin.h"
#include "plugins/Visualization.h"

#include <QAction>
#include <QTest>

using namespace newamp;

namespace {
// Records what a plugin asks of the host.
class TestHost final : public PluginHost
{
public:
    void addTracks(const QList<Track> &tracks) override { added += tracks; }
    void playTracks(const QList<Track> &tracks) override { added += tracks; }
    QWidget *dialogParent() const override { return nullptr; }
    std::unique_ptr<QSettings> settings(const QString &) const override { return nullptr; }
    void addPlaybackObserver(PlaybackObserver *) override {}
    void addLibrary(LibraryProvider *) override {}
    void addDspEffect(DspEffect *effect) override { effects << effect; }
    void addVisualization(Visualization *visualization) override { visualizations << visualization; }
    void libraryChanged(LibraryProvider *) override {}
    void libraryStatusChanged(LibraryProvider *) override {}
    void showLibrary(LibraryProvider *) override {}

    QList<Track> added;
    QList<DspEffect *> effects;
    QList<Visualization *> visualizations;
};

const PluginInfo *find(const PluginManager &manager, const QString &id)
{
    for (const PluginInfo &info : manager.available())
        if (info.id == id)
            return &info;
    return nullptr;
}
} // namespace

class TestPlugins : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void discoversWithoutLoading()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        QCOMPARE(manager.available().size(), 2);
        const PluginInfo *info = find(manager, QStringLiteral("dummy"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Dummy source"));
        QCOMPARE(info->category, QStringLiteral("source")); // the default
        QCOMPARE(find(manager, QStringLiteral("dummygeneral"))->category, QStringLiteral("general"));
        QVERIFY(manager.loaded().isEmpty());
    }

    void loadsOnlyEnabledPlugins()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        QVERIFY(manager.loadEnabled({}).isEmpty());

        const QList<MediaSource *> sources = manager.loadEnabled({QStringLiteral("dummy")});
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources.first()->id(), QStringLiteral("dummy"));
        QCOMPARE(manager.loaded().size(), 1);
        QCOMPARE(manager.loaded().first().info.id, QStringLiteral("dummy"));
    }

    void initializesWithHost()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        TestHost host;
        manager.loadEnabled({QStringLiteral("dummy")}, &host);
        QCOMPARE(manager.loaded().size(), 1);

        const QList<QAction *> actions = manager.loaded().first().plugin->actions();
        QCOMPARE(actions.size(), 1);
        actions.first()->trigger();
        QCOMPARE(host.added.size(), 1);
        QCOMPARE(host.added.first().sourceId, QStringLiteral("dummy"));
    }

    void generalPluginOffersEffectsWithoutASource()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        TestHost host;
        QVERIFY(manager.loadEnabled({QStringLiteral("dummygeneral")}, &host).isEmpty());
        QCOMPARE(manager.loaded().size(), 1);
        QCOMPARE(host.effects.size(), 1);
        QCOMPARE(host.effects.first()->id(), QStringLiteral("dummygeneral.silence"));
        QCOMPARE(host.visualizations.size(), 1);
        QCOMPARE(host.visualizations.first()->name(), QStringLiteral("Black"));

        float samples[] = {0.5f, -0.5f};
        host.effects.first()->configure(44100, 2);
        host.effects.first()->process(samples, 1);
        QCOMPARE(samples[0], 0.0f);
    }

    void ignoresMissingDirectories()
    {
        PluginManager manager;
        manager.discover({QStringLiteral("/nonexistent/newamp/plugins")});
        QVERIFY(manager.available().isEmpty());
    }
};

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
