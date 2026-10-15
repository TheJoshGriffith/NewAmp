#include "core/MediaSource.h"
#include "plugins/PluginManager.h"

#include <QTest>

using namespace newamp;

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
    }

    void loadsOnlyEnabledPlugins()
    {
        PluginManager manager;
        manager.discover({QStringLiteral(NEWAMP_TEST_PLUGIN_DIR)});
        QVERIFY(manager.loadEnabled({}).isEmpty());

        const QList<MediaSource *> sources = manager.loadEnabled({QStringLiteral("dummy")});
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources.first()->id(), QStringLiteral("dummy"));
    }

    void ignoresMissingDirectories()
    {
        PluginManager manager;
        manager.discover({QStringLiteral("/nonexistent/newamp/plugins")});
        QVERIFY(manager.available().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPlugins)
#include "tst_plugins.moc"
