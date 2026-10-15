#include "plugins/LibraryProvider.h"
#include "plugins/PluginHost.h"
#include "skin/DefaultSkin.h"
#include "ui/LibraryWindow.h"

#include <QTest>

using namespace newamp;

namespace {

class RecordingHost final : public PluginHost
{
public:
    void addTracks(const QList<Track> &tracks) override { added += tracks; }
    void playTracks(const QList<Track> &tracks) override { played += tracks; }
    QWidget *dialogParent() const override { return nullptr; }
    std::unique_ptr<QSettings> settings(const QString &) const override { return nullptr; }
    void addPlaybackObserver(PlaybackObserver *) override {}
    void addLibrary(LibraryProvider *) override {}
    void libraryChanged(LibraryProvider *) override {}
    void libraryStatusChanged(LibraryProvider *) override {}
    void showLibrary(LibraryProvider *) override {}

    QList<Track> added;
    QList<Track> played;
};

Track track(const QString &title)
{
    Track t;
    t.title = title;
    t.location = QUrl(QStringLiteral("test:") + title);
    return t;
}

// Two artists with albums of tracks. Replies can be held back to test
// answers that arrive after the user has moved on.
class FakeLibrary final : public LibraryProvider
{
public:
    QString libraryName() const override { return QStringLiteral("Fake"); }

    void listItems(const QString &id, const ItemsCallback &done) override
    {
        QList<LibraryItem> items;
        if (id.isEmpty()) {
            items = {{QStringLiteral("artist:a"), QStringLiteral("Alpha"), {}, true, {}},
                     {QStringLiteral("artist:b"), QStringLiteral("Beta"), {}, true, {}}};
        } else if (id.startsWith(QLatin1String("artist:"))) {
            items = {{QStringLiteral("album:") + id.mid(7), QStringLiteral("Album ") + id.mid(7), {}, true, {}}};
        } else {
            for (const Track &t : tracksOf(id))
                items.append({{}, t.title, {}, false, t});
        }
        if (holdReplies)
            held.append([done, items] { done(items); });
        else
            done(items);
    }

    void folderTracks(const QString &id, const TracksCallback &done) override { done(tracksOf(id)); }
    QString status() const override { return {}; }

    static QList<Track> tracksOf(const QString &id)
    {
        if (!id.startsWith(QLatin1String("album:")))
            return {};
        return {track(id.mid(6) + QStringLiteral("1")), track(id.mid(6) + QStringLiteral("2"))};
    }

    bool holdReplies = false;
    QList<std::function<void()>> held;
};

} // namespace

class TestLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void opensFoldersAndGoesBack()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);

        // A single library is the top level.
        QCOMPARE(window.currentPath(), QStringLiteral("Fake"));
        QCOMPARE(window.itemCount(), 2);

        QTest::keyClick(&window, Qt::Key_Down); // Beta
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake / Beta"));
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake / Beta / Album b"));
        QCOMPARE(window.itemCount(), 2);

        // Enter on a track plays it.
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(host.played.size(), 1);
        QCOMPARE(host.played.first().title, QStringLiteral("b1"));

        QTest::keyClick(&window, Qt::Key_Backspace);
        QTest::keyClick(&window, Qt::Key_Backspace);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake"));
        // Can't go above the only library.
        QTest::keyClick(&window, Qt::Key_Backspace);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake"));
    }

    void typingFiltersTheFolder()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);

        QTest::keyClicks(&window, QStringLiteral("bet"));
        QCOMPARE(window.itemCount(), 1);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake   [bet]"));
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.itemCount(), 2);
    }

    void addsWholeFolders()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);

        QTest::keyClick(&window, Qt::Key_Return); // Alpha
        QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
        window.addSelection();
        QCOMPARE(host.added.size(), 2);
        QCOMPARE(host.added.at(1).title, QStringLiteral("a2"));
    }

    void dropsRepliesForFoldersAlreadyLeft()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);

        library.holdReplies = true;
        QTest::keyClick(&window, Qt::Key_Return); // Alpha, reply held
        QTest::keyClick(&window, Qt::Key_Backspace);
        library.holdReplies = false;
        window.reload(&library); // the top level answers at once
        QCOMPARE(window.itemCount(), 2);
        for (const auto &reply : std::as_const(library.held))
            reply(); // Alpha's late answer must not replace the top level
        QCOMPARE(window.currentPath(), QStringLiteral("Fake"));
        QCOMPARE(window.itemCount(), 2);
    }

    void showsNoRowsWhileAFolderLoads()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);
        QCOMPARE(window.itemCount(), 2);

        // Until the reply arrives the old rows are gone, and painting,
        // keys and clicks must not reach them.
        library.holdReplies = true;
        QTest::keyClick(&window, Qt::Key_Return); // Alpha, reply held
        QCOMPARE(window.itemCount(), 0);
        window.grab();
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_Right);
        window.playSelection();
        QCOMPARE(host.played.size(), 0);

        library.holdReplies = false;
        for (const auto &reply : std::as_const(library.held))
            reply();
        QCOMPARE(window.itemCount(), 1);
        window.grab();
    }

    void listsSeveralLibraries()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary first;
        FakeLibrary second;
        LibraryWindow window(&skin, host);
        window.addLibrary(&first);
        window.addLibrary(&second);
        QCOMPARE(window.currentPath(), QStringLiteral("Library"));
        QCOMPARE(window.itemCount(), 2);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.currentPath(), QStringLiteral("Library / Fake"));
    }
};

QTEST_MAIN(TestLibrary)
#include "tst_library.moc"
