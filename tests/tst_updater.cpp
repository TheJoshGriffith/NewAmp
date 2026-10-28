#include "core/Updater.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace newamp;

namespace {

const QByteArray Hash = QByteArray(64, 'a');

QJsonObject asset(const QString &name, const QString &digest = {})
{
    QJsonObject asset{{QStringLiteral("name"), name},
                      {QStringLiteral("browser_download_url"),
                       QStringLiteral("https://github.com/o/r/releases/download/x/") + name}};
    if (!digest.isEmpty())
        asset.insert(QStringLiteral("digest"), digest);
    return asset;
}

QJsonObject release(const QString &tag, bool prerelease = false, QJsonArray assets = {})
{
    return {{QStringLiteral("tag_name"), tag},
            {QStringLiteral("prerelease"), prerelease},
            {QStringLiteral("draft"), false},
            {QStringLiteral("body"), QStringLiteral("Notes for ") + tag},
            {QStringLiteral("html_url"), QStringLiteral("https://github.com/o/r/releases/tag/") + tag},
            {QStringLiteral("assets"), assets}};
}

QByteArray json(const QJsonArray &releases)
{
    return QJsonDocument(releases).toJson();
}

} // namespace

class TestUpdater : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compareVersions_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("order");
        QTest::newRow("equal") << "0.1.0" << "v0.1.0" << 0;
        QTest::newRow("patch") << "0.1.0" << "0.1.1" << -1;
        QTest::newRow("numeric not text") << "0.10.0" << "0.9.0" << 1;
        QTest::newRow("short form") << "1.2" << "1.2.0" << 0;
        QTest::newRow("rc before release") << "0.2.0-rc.1" << "0.2.0" << -1;
        QTest::newRow("rc order") << "0.2.0-rc.2" << "0.2.0-rc.10" << -1;
        QTest::newRow("beta before rc") << "0.2.0-beta.3" << "0.2.0-rc.1" << -1;
        QTest::newRow("rc after older release") << "0.2.0-rc.1" << "0.1.9" << 1;
        QTest::newRow("longer prerelease later") << "1.0.0-rc" << "1.0.0-rc.1" << -1;
        QTest::newRow("build metadata ignored") << "1.0.0+abc" << "1.0.0" << 0;
    }

    void compareVersions()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, order);
        QCOMPARE(Updater::compareVersions(a, b), order);
        QCOMPARE(Updater::compareVersions(b, a), -order);
    }

    void newestStableRelease()
    {
        const QByteArray list = json({release(QStringLiteral("v0.3.0-rc.1"), true),
                                      release(QStringLiteral("v0.2.0"), false,
                                              {asset(QStringLiteral("NewAmp-macOS.dmg"), "sha256:" + Hash),
                                               asset(QStringLiteral("SHA256SUMS"))}),
                                      release(QStringLiteral("v0.1.1"))});
        const UpdateRelease found = Updater::newestRelease(list, QStringLiteral("0.1.0"), false,
                                                           QStringLiteral("NewAmp-macOS.dmg"));
        QCOMPARE(found.version, QStringLiteral("0.2.0"));
        QVERIFY(!found.prerelease);
        QCOMPARE(found.assetName, QStringLiteral("NewAmp-macOS.dmg"));
        QCOMPARE(found.sha256, Hash);
        QCOMPARE(found.checksums.fileName(), QStringLiteral("SHA256SUMS"));
        QCOMPARE(found.notes, QStringLiteral("Notes for v0.2.0"));
        QCOMPARE(found.page.host(), QStringLiteral("github.com"));
    }

    void releaseCandidatesWhenAsked()
    {
        const QByteArray list = json({release(QStringLiteral("v0.2.0")), release(QStringLiteral("v0.3.0-rc.2"), true),
                                      release(QStringLiteral("v0.3.0-rc.1"), true)});
        const UpdateRelease found = Updater::newestRelease(list, QStringLiteral("0.1.0"), true, {});
        QCOMPARE(found.version, QStringLiteral("0.3.0-rc.2"));
        QVERIFY(found.prerelease);
        QVERIFY(!found.asset.isValid());
        // A tag with a suffix counts as a pre-release even if not marked one.
        const UpdateRelease stable = Updater::newestRelease(
            json({release(QStringLiteral("v0.3.0-rc.3"))}), QStringLiteral("0.2.0"), false, {});
        QVERIFY(!stable.isValid());
    }

    void finalReleaseReplacesItsCandidate()
    {
        const QByteArray list = json({release(QStringLiteral("v0.3.0"))});
        QCOMPARE(Updater::newestRelease(list, QStringLiteral("0.3.0-rc.2"), true, {}).version, QStringLiteral("0.3.0"));
        QVERIFY(!Updater::newestRelease(list, QStringLiteral("0.3.0"), true, {}).isValid());
    }

    void skipsDraftsOddTagsAndForeignHosts()
    {
        QJsonObject draft = release(QStringLiteral("v9.0.0"));
        draft.insert(QStringLiteral("draft"), true);
        QJsonObject foreign = release(QStringLiteral("v0.5.0"), false, {});
        QJsonObject evil = asset(QStringLiteral("NewAmp-macOS.dmg"));
        evil.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://example.com/NewAmp-macOS.dmg"));
        foreign.insert(QStringLiteral("assets"), QJsonArray{evil});
        const QByteArray list = json({draft, release(QStringLiteral("nightly")), foreign});
        const UpdateRelease found = Updater::newestRelease(list, QStringLiteral("0.1.0"), false,
                                                           QStringLiteral("NewAmp-macOS.dmg"));
        QCOMPARE(found.version, QStringLiteral("0.5.0"));
        QVERIFY(!found.asset.isValid());
        QVERIFY(!Updater::newestRelease("not json", QStringLiteral("0.1.0"), true, {}).isValid());
    }

    void checksumListing()
    {
        const QByteArray listing = QByteArray(64, 'B') + "  NewAmp-macOS.dmg\n" + QByteArray(64, 'c')
            + " *NewAmp-Windows-x64.zip\n" + "garbage line\n";
        QCOMPARE(Updater::checksumFor(listing, QStringLiteral("NewAmp-macOS.dmg")), QByteArray(64, 'b'));
        QCOMPARE(Updater::checksumFor(listing, QStringLiteral("NewAmp-Windows-x64.zip")), QByteArray(64, 'c'));
        QVERIFY(Updater::checksumFor(listing, QStringLiteral("NewAmp-x86_64.AppImage")).isEmpty());
    }

    void onlyGitHub()
    {
        QVERIFY(Updater::isGitHubUrl(QUrl(QStringLiteral("https://api.github.com/repos/o/r/releases"))));
        QVERIFY(Updater::isGitHubUrl(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/x"))));
        QVERIFY(!Updater::isGitHubUrl(QUrl(QStringLiteral("http://github.com/o/r"))));
        QVERIFY(!Updater::isGitHubUrl(QUrl(QStringLiteral("https://github.com.example.com/x"))));
        QVERIFY(!Updater::isGitHubUrl(QUrl(QStringLiteral("https://evilgithubusercontent.com/x"))));
    }

    void prerelease()
    {
        QVERIFY(Updater::isPrerelease(QStringLiteral("v0.2.0-rc.1")));
        QVERIFY(!Updater::isPrerelease(QStringLiteral("0.2.0")));
    }
};

QTEST_MAIN(TestUpdater)
#include "tst_updater.moc"
