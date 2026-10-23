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

using Kind = LibraryItem::Kind;

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
            items = {{QStringLiteral("artist:a"), QStringLiteral("Alpha"), {}, Kind::Artist, {}},
                     {QStringLiteral("artist:b"), QStringLiteral("Beta"), {}, Kind::Artist, {}}};
        } else if (id.startsWith(QLatin1String("artist:"))) {
            items = {{QStringLiteral("album:") + id.mid(7), QStringLiteral("Album ") + id.mid(7), {}, Kind::Album, {}}};
        } else {
            for (const Track &t : tracksOf(id))
                items.append({{}, t.title, {}, Kind::Track, t});
        }
        if (holdReplies)
            held.append([done, items] { done(items); });
        else
            done(items);
    }

    void folderTracks(const QString &id, const TracksCallback &done) override { done(tracksOf(id)); }
    QString status() const override { return {}; }

    bool canSearch() const override { return searchable; }
    // Finds artist a, its album and its first track, in no particular order.
    void search(const QString &scopeId, const QString &query, const ItemsCallback &done) override
    {
        lastScope = scopeId;
        lastQuery = query;
        const Track first = tracksOf(QStringLiteral("album:a")).first();
        done({{{}, first.title, {}, Kind::Track, first},
              {QStringLiteral("artist:a"), QStringLiteral("Alpha"), {}, Kind::Artist, {}},
              {QStringLiteral("album:a"), QStringLiteral("Album a"), {}, Kind::Album, {}}});
    }

    static QList<Track> tracksOf(const QString &id)
    {
        if (!id.startsWith(QLatin1String("album:")))
            return {};
        return {track(id.mid(6) + QStringLiteral("1")), track(id.mid(6) + QStringLiteral("2"))};
    }

    bool holdReplies = false;
    QList<std::function<void()>> held;
    bool searchable = true;
    QString lastScope;
    QString lastQuery;
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

    void searchesAndGroupsResults()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);
        QTest::keyClick(&window, Qt::Key_Return); // Alpha
        const QString alpha = window.currentPath();

        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(&window, QStringLiteral("al"));
        QCOMPARE(window.currentPath(), QStringLiteral("Search Fake: al_"));
        QCOMPARE(window.itemCount(), 1); // still the folder underneath
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(library.lastQuery, QStringLiteral("al"));
        QCOMPARE(library.lastScope, QStringLiteral("artist:a"));
        QCOMPARE(window.currentPath(), QStringLiteral("Fake / Alpha / Search \"al\""));
        // Artists, Albums and Tracks, each under a title row.
        QCOMPARE(window.itemCount(), 6);

        // The cursor starts on Alpha, not the title above it.
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake / Alpha / Search \"al\" / Alpha"));
        QTest::keyClick(&window, Qt::Key_Backspace);

        // Title rows are skipped when queueing everything.
        QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
        window.addSelection();
        QCOMPARE(host.added.size(), 3); // Album a's two tracks, then a1
        QCOMPARE(host.added.at(2).title, QStringLiteral("a1"));

        // Filtering hides the titles; searching again replaces the results.
        QTest::keyClicks(&window, QStringLiteral("a1"));
        QCOMPARE(window.itemCount(), 1);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier); // seeded with "a1"
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(library.lastQuery, QStringLiteral("a1"));
        QCOMPARE(library.lastScope, QStringLiteral("artist:a"));
        QCOMPARE(window.currentPath(), QStringLiteral("Fake / Alpha / Search \"a1\""));
        QTest::keyClick(&window, Qt::Key_Backspace);
        QCOMPARE(window.currentPath(), alpha);
    }

    void escapeCancelsASearch()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);
        window.beginSearch();
        QTest::keyClicks(&window, QStringLiteral("x"));
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(window.currentPath(), QStringLiteral("Fake"));
        QVERIFY(library.lastQuery.isEmpty());
    }

    void librariesWithoutSearchJustFilter()
    {
        const Skin skin = DefaultSkin::create();
        RecordingHost host;
        FakeLibrary library;
        library.searchable = false;
        LibraryWindow window(&skin, host);
        window.addLibrary(&library);
        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClicks(&window, QStringLiteral("bet"));
        QCOMPARE(window.currentPath(), QStringLiteral("Fake   [bet]"));
        QTest::keyClick(&window, Qt::Key_Return);
        QVERIFY(library.lastQuery.isEmpty());
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
