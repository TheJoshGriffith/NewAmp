#include "core/LocalFileSource.h"
#include "core/LocalTagEditor.h"
#include "skin/DefaultSkin.h"
#include "ui/FileInfoWindow.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace newamp;

namespace {

QString tagValue(const TrackInfo &info, const QString &key)
{
    for (const TagField &field : info.tags)
        if (field.key == key)
            return field.value;
    return {};
}

QString detail(const TrackInfo &info, const QString &name)
{
    for (const auto &[label, value] : info.details)
        if (label == name)
            return value;
    return {};
}

} // namespace

// Reads and writes real (tiny) files of every format NewAmp tags, made with
// ffmpeg and carrying title "Original", artist "Someone", album "First".
class TestTags : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString copyFixture(const QString &name)
    {
        const QString to = m_dir.filePath(name);
        QFile::remove(to);
        QFile::copy(QStringLiteral(NEWAMP_TAG_FIXTURES "/") + name, to);
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
        return to;
    }

private Q_SLOTS:
    void readsTagsAndDetails_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("format");
        QTest::newRow("mp3") << "tone.mp3" << "MP3";
        QTest::newRow("flac") << "tone.flac" << "FLAC";
        QTest::newRow("ogg") << "tone.ogg" << "Ogg Vorbis";
        QTest::newRow("opus") << "tone.opus" << "Opus";
        QTest::newRow("m4a") << "tone.m4a" << "AAC (MP4)";
    }

    void readsTagsAndDetails()
    {
        QFETCH(QString, file);
        QFETCH(QString, format);
        const TrackInfo info = LocalTagEditor::read(copyFixture(file));
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QVERIFY(info.canEdit);
        QCOMPARE(tagValue(info, QStringLiteral("TITLE")), QStringLiteral("Original"));
        QCOMPARE(tagValue(info, QStringLiteral("ARTIST")), QStringLiteral("Someone"));
        QCOMPARE(tagValue(info, QStringLiteral("ALBUM")), QStringLiteral("First"));
        QCOMPARE(info.tags.size(), standardTagKeys().size());
        QCOMPARE(detail(info, QStringLiteral("Format")), format);
        QCOMPARE(detail(info, QStringLiteral("Channels")), QStringLiteral("Mono"));
        QVERIFY(!detail(info, QStringLiteral("Length")).isEmpty());
    }

    void writesAndReadsBack_data() { readsTagsAndDetails_data(); }

    void writesAndReadsBack()
    {
        QFETCH(QString, file);
        const QString path = copyFixture(file);
        const QString length = detail(LocalTagEditor::read(path), QStringLiteral("Length"));

        // Unicode, a removal, numbers, and a comment long enough that the
        // tag outgrows its padding and the file has to be rewritten.
        const QString comment = QString(QStringLiteral("long comment ")).repeated(400);
        const QString error = LocalTagEditor::write(path, {
            {QStringLiteral("TITLE"), QStringLiteral("Größer — 大きい")},
            {QStringLiteral("ALBUM"), QString()},
            {QStringLiteral("ALBUMARTIST"), QStringLiteral("Various")},
            {QStringLiteral("TRACKNUMBER"), QStringLiteral("3")},
            {QStringLiteral("DATE"), QStringLiteral("1999")},
            {QStringLiteral("GENRE"), QStringLiteral("Rock; Pop")},
            {QStringLiteral("COMMENT"), comment},
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const TrackInfo info = LocalTagEditor::read(path);
        QCOMPARE(tagValue(info, QStringLiteral("TITLE")), QStringLiteral("Größer — 大きい"));
        QCOMPARE(tagValue(info, QStringLiteral("ARTIST")), QStringLiteral("Someone")); // untouched
        QCOMPARE(tagValue(info, QStringLiteral("ALBUM")), QString());
        QCOMPARE(tagValue(info, QStringLiteral("ALBUMARTIST")), QStringLiteral("Various"));
        QCOMPARE(tagValue(info, QStringLiteral("TRACKNUMBER")), QStringLiteral("3"));
        QCOMPARE(tagValue(info, QStringLiteral("DATE")), QStringLiteral("1999"));
        QCOMPARE(tagValue(info, QStringLiteral("GENRE")), QStringLiteral("Rock; Pop"));
        QCOMPARE(tagValue(info, QStringLiteral("COMMENT")), comment.trimmed());
        // The audio is still there and still the same length.
        QCOMPARE(detail(info, QStringLiteral("Length")), length);
    }

    void keepsTheId3Version()
    {
        const QString path = copyFixture(QStringLiteral("tone.mp3"));
        QCOMPARE(detail(LocalTagEditor::read(path), QStringLiteral("Tags")), QStringLiteral("ID3v2.3"));
        QVERIFY(LocalTagEditor::write(path, {{QStringLiteral("TITLE"), QStringLiteral("New")}}).isEmpty());
        QCOMPARE(detail(LocalTagEditor::read(path), QStringLiteral("Tags")), QStringLiteral("ID3v2.3"));
    }

    void addsTagsToAnUntaggedWav()
    {
        const QString path = copyFixture(QStringLiteral("tone.wav"));
        QVERIFY(LocalTagEditor::write(path, {{QStringLiteral("TITLE"), QStringLiteral("Wave")}}).isEmpty());
        QCOMPARE(tagValue(LocalTagEditor::read(path), QStringLiteral("TITLE")), QStringLiteral("Wave"));
    }

    void reportsReadOnlyAndMissingFiles()
    {
        const QString path = copyFixture(QStringLiteral("tone.flac"));
        QFile::setPermissions(path, QFile::ReadOwner);
        const TrackInfo info = LocalTagEditor::read(path);
        if (QFileInfo(path).isWritable())
            QSKIP("running as a user who can write anything");
        QVERIFY(!info.canEdit);
        QVERIFY(!info.note.isEmpty());

        QVERIFY(!LocalTagEditor::read(m_dir.filePath(QStringLiteral("gone.mp3"))).error.isEmpty());
    }

    void windowEditsAndSaves()
    {
        const QString path = copyFixture(QStringLiteral("tone.ogg"));
        LocalFileSource source;
        const Skin skin = DefaultSkin::create();
        FileInfoWindow window(&skin, [&](const Track &) { return source.metadataEditor(); });
        QSignalSpy updated(&window, &FileInfoWindow::trackUpdated);

        window.showTrack(LocalFileSource::trackForFile(path));
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("Original"));
        QVERIFY(!window.isModified());

        window.setTag(QStringLiteral("ARTIST"), QStringLiteral("Someone Else"));
        QVERIFY(window.isModified());
        window.revert();
        QVERIFY(!window.isModified());

        window.setTag(QStringLiteral("ARTIST"), QStringLiteral("Someone Else"));
        window.save();
        QCOMPARE(window.message(), QStringLiteral("Saved"));
        QVERIFY(!window.isModified());
        QCOMPARE(updated.count(), 1);
        const Track track = updated.first().at(1).value<Track>();
        QCOMPARE(track.artist, QStringLiteral("Someone Else"));
        QCOMPARE(track.title, QStringLiteral("Original"));
        QCOMPARE(tagValue(LocalTagEditor::read(path), QStringLiteral("ARTIST")), QStringLiteral("Someone Else"));
    }

    void windowTypesIntoTheFocusedField()
    {
        const QString path = copyFixture(QStringLiteral("tone.flac"));
        LocalFileSource source;
        const Skin skin = DefaultSkin::create();
        FileInfoWindow window(&skin, [&](const Track &) { return source.metadataEditor(); });
        window.showTrack(LocalFileSource::trackForFile(path));

        // The title is focused and selected: typing replaces it, and player
        // keys such as Z are text here.
        QTest::keyClicks(&window, QStringLiteral("Zed"));
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("Zed"));
        QTest::keyClick(&window, Qt::Key_Backspace);
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("Ze"));
        // Tab moves to the artist, whose text is selected.
        QTest::keyClick(&window, Qt::Key_Tab);
        QTest::keyClicks(&window, QStringLiteral("X"));
        QCOMPARE(window.tag(QStringLiteral("ARTIST")), QStringLiteral("X"));
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(tagValue(LocalTagEditor::read(path), QStringLiteral("TITLE")), QStringLiteral("Ze"));
        QCOMPARE(tagValue(LocalTagEditor::read(path), QStringLiteral("ARTIST")), QStringLiteral("X"));
    }

    void windowKeepsReadOnlyFields()
    {
        // A source that lets the title change but not the album artist.
        struct Editor : MetadataEditor
        {
            QList<TagField> saved;
            void readInfo(const Track &, const InfoCallback &done) override
            {
                TrackInfo info;
                info.canEdit = true;
                info.tags = {{QStringLiteral("ALBUMARTIST"), QStringLiteral("Band"), false, true},
                             {QStringLiteral("TITLE"), QStringLiteral("Song")},
                             {QStringLiteral("ALBUM"), QStringLiteral("Record"), true}};
                done(info);
            }
            void writeTags(const Track &, const QList<TagField> &changes, const SaveCallback &done) override
            {
                saved = changes;
                done({});
            }
        } editor;
        const Skin skin = DefaultSkin::create();
        FileInfoWindow window(&skin, [&](const Track &) { return &editor; });
        window.showTrack(Track());

        window.setTag(QStringLiteral("ALBUMARTIST"), QStringLiteral("Other"));
        QCOMPARE(window.tag(QStringLiteral("ALBUMARTIST")), QStringLiteral("Band"));
        // Focus skips the read-only field: typing replaces the title.
        QTest::keyClicks(&window, QStringLiteral("New"));
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("New"));
        window.save();
        QCOMPARE(editor.saved.size(), 1);
        QCOMPARE(editor.saved.first().key, QStringLiteral("TITLE"));
    }

    void windowWithoutAnEditorIsReadOnly()
    {
        const Skin skin = DefaultSkin::create();
        FileInfoWindow window(&skin, [](const Track &) { return nullptr; });
        Track track;
        track.sourceId = QStringLiteral("spotify");
        track.location = QUrl(QStringLiteral("spotify:track:1"));
        track.title = QStringLiteral("Song");
        window.showTrack(track);
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("Song"));
        window.setTag(QStringLiteral("TITLE"), QStringLiteral("Other"));
        QCOMPARE(window.tag(QStringLiteral("TITLE")), QStringLiteral("Song"));
        QVERIFY(!window.info().canEdit);
    }
};

QTEST_MAIN(TestTags)
#include "tst_tags.moc"
