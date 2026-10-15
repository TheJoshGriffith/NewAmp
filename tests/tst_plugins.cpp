#include "core/MediaSource.h"
#include "plugins/PluginHost.h"
#include "plugins/PluginManager.h"
#include "plugins/SourcePlugin.h"

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
    void libraryChanged(LibraryProvider *) override {}
    void libraryStatusChanged(LibraryProvider *) override {}
    void showLibrary(LibraryProvider *) override {}

    QList<Track> added;
};
} // namespace

class TestPlugins : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void discoversWithoutLoading()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        QCOMPARE(manager.available().size(), 1);
        const PluginInfo &info = manager.available().first();
        QCOMPARE(info.id, QStringLiteral("dummy"));
        QCOMPARE(info.name, QStringLiteral("Dummy source"));
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

    void ignoresMissingDirectories()
    {
        PluginManager manager;
        manager.discover({QStringLiteral("/nonexistent/newamp/plugins")});
        QVERIFY(manager.available().isEmpty());
    }
};

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
