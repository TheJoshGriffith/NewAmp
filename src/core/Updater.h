#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace newamp {

// A newer release found on GitHub.
struct UpdateRelease
{
    QString version; // without the tag's "v": 0.2.0-rc.1
    QString notes;   // the release's description, in Markdown
    QUrl page;       // the release on github.com
    bool prerelease = false;
    // This platform's download; empty when the release has none, and then
    // only the page is offered.
    QString assetName;
    QUrl asset;
    QByteArray sha256;  // hex, from GitHub's own digest of the asset
    QUrl checksums;     // the release's SHA256SUMS, when there's no digest

    bool isValid() const { return !version.isEmpty(); }
};

// Updates NewAmp from its GitHub releases and nowhere else: checks the
// repository's releases, downloads this platform's build, checks its
// SHA-256, and puts it in place of the running copy. Tags like v0.2.0 are
// releases; v0.2.0-rc.1 are pre-releases, offered only when asked for.
//
// Replacing itself works for the macOS app in a folder the user can write
// to, the Windows zip build, and the Linux AppImage. Anything else (a
// system package, a read-only or translocated app) gets the release page.
class Updater : public QObject
{
    Q_OBJECT

public:
    // `repository` is owner/name on github.com.
    Updater(const QString &repository, const QString &currentVersion, QObject *parent = nullptr);
    ~Updater() override;

    void setIncludePrereleases(bool on) { m_prereleases = on; }
    bool includePrereleases() const { return m_prereleases; }
    QString currentVersion() const { return m_current; }

    // Asks GitHub for releases; updateAvailable(), upToDate() or
    // checkFailed() follows.
    void check();
    // Fetches the release's download into a temporary file and checks it;
    // downloaded() or downloadFailed() follows.
    void download(const UpdateRelease &release);
    void cancel();
    // Whether this copy can replace itself, rather than send people to the
    // release page.
    bool canInstall() const;
    // Puts the downloaded build in place and starts a helper that relaunches
    // NewAmp once this process has exited. The caller quits right after.
    bool install(QString *error);

    // Semantic version order: 0.2.0-rc.1 < 0.2.0-rc.2 < 0.2.0 < 0.2.1.
    // A leading "v" is ignored. Negative, zero or positive like strcmp.
    static int compareVersions(const QString &a, const QString &b);
    static bool isPrerelease(const QString &version);
    // The newest release in GitHub's release list that's newer than
    // `current`, or an invalid one.
    static UpdateRelease newestRelease(const QByteArray &json, const QString &current, bool prereleases,
                                       const QString &assetName);
    // The hash for `fileName` in a sha256sum listing, lower case hex.
    static QByteArray checksumFor(const QByteArray &listing, const QString &fileName);
    // The CI build's file name for this platform, empty where there's none.
    static QString platformAssetName();
    // Hosts updates may come from: GitHub and its download servers.
    static bool isGitHubUrl(const QUrl &url);

Q_SIGNALS:
    void updateAvailable(const UpdateRelease &release);
    void upToDate();
    void checkFailed(const QString &error);
    void downloadProgress(qint64 received, qint64 total);
    void downloaded();
    void downloadFailed(const QString &error);

private:
    QNetworkReply *get(const QUrl &url);
    void fetchAsset();
    void finishDownload(QNetworkReply *reply);
    void failDownload(const QString &error);
    bool installMac(QString *error);
    bool installWindows(QString *error);
    bool installAppImage(QString *error);

    QString m_repository;
    QString m_current;
    bool m_prereleases = false;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    UpdateRelease m_release;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
};

} // namespace newamp
