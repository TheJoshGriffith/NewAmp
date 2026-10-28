#include "core/Updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <cstdio>

namespace newamp {

namespace {

const QString ChecksumsName = QStringLiteral("SHA256SUMS");

struct ParsedVersion
{
    QList<int> core;
    QStringList prerelease;
};

ParsedVersion parseVersion(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        text.remove(0, 1);
    text = text.section(QLatin1Char('+'), 0, 0); // build metadata doesn't count
    ParsedVersion version;
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        version.prerelease = text.mid(dash + 1).split(QLatin1Char('.'));
        text.truncate(dash);
    }
    for (const QString &part : text.split(QLatin1Char('.')))
        version.core.append(part.toInt());
    while (version.core.size() < 3)
        version.core.append(0);
    return version;
}

// Whether `dir` takes new files, which is what replacing the app needs.
bool isWritableDir(const QString &dir)
{
    QTemporaryFile probe(dir + QStringLiteral("/.newamp-update-XXXXXX"));
    return probe.open();
}

// The .app bundle the running executable sits in, or empty.
QString macBundlePath()
{
    QDir dir(QCoreApplication::applicationDirPath()); // .../NewAmp.app/Contents/MacOS
    if (!dir.cdUp() || !dir.cdUp() || !dir.dirName().endsWith(QLatin1String(".app")))
        return {};
    return dir.absolutePath();
}

bool run(const QString &program, const QStringList &arguments)
{
    return QProcess::execute(program, arguments) == 0;
}

} // namespace

Updater::Updater(const QString &repository, const QString &currentVersion, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_current(currentVersion)
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setRedirectPolicy(QNetworkRequest::UserVerifiedRedirectPolicy);
}

Updater::~Updater()
{
    cancel();
}

QNetworkReply *Updater::get(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NewAmp/%1 (+https://github.com/%2)").arg(m_current, m_repository));
    if (url.host() == QLatin1String("api.github.com"))
        request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_network->get(request);
    // Downloads redirect to GitHub's file servers; nothing else is followed.
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &target) {
        if (isGitHubUrl(target))
            Q_EMIT reply->redirectAllowed();
        else
            reply->abort();
    });
    return reply;
}

void Updater::check()
{
    if (m_reply)
        return;
    QNetworkReply *reply = get(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(m_repository)));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT checkFailed(reply->errorString());
            return;
        }
        const UpdateRelease release = newestRelease(reply->readAll(), m_current, m_prereleases, platformAssetName());
        if (release.isValid())
            Q_EMIT updateAvailable(release);
        else
            Q_EMIT upToDate();
    });
}

void Updater::download(const UpdateRelease &release)
{
    cancel();
    m_release = release;
    if (!release.asset.isValid()) {
        failDownload(tr("This release has no download for this computer."));
        return;
    }
    const QString dir = QDir::temp().filePath(QStringLiteral("NewAmp-update"));
    QDir().mkpath(dir);
    m_file.setFileName(QDir(dir).filePath(release.assetName));
    if (!release.sha256.isEmpty()) {
        fetchAsset();
        return;
    }
    // Nothing gets installed without a hash to check it against.
    if (!release.checksums.isValid()) {
        failDownload(tr("The release has no checksum for its download."));
        return;
    }
    QNetworkReply *reply = get(release.checksums);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            failDownload(reply->errorString());
            return;
        }
        m_release.sha256 = checksumFor(reply->readAll(), m_release.assetName);
        if (m_release.sha256.isEmpty())
            failDownload(tr("The release's checksums don't list %1.").arg(m_release.assetName));
        else
            fetchAsset();
    });
}

void Updater::fetchAsset()
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failDownload(m_file.errorString());
        return;
    }
    m_hash.reset();
    QNetworkReply *reply = get(m_release.asset);
    m_reply = reply;
    // Written as it arrives: builds are tens of megabytes.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray data = reply->readAll();
        m_hash.addData(data);
        m_file.write(data);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, &Updater::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishDownload(reply); });
}

void Updater::finishDownload(QNetworkReply *reply)
{
    reply->deleteLater();
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        failDownload(reply->errorString());
        return;
    }
    const QByteArray rest = reply->readAll();
    m_hash.addData(rest);
    m_file.write(rest);
    m_file.close();
    if (m_hash.result().toHex() != m_release.sha256.toLower()) {
        failDownload(tr("The download doesn't match its checksum."));
        return;
    }
    Q_EMIT downloaded();
}

void Updater::failDownload(const QString &error)
{
    m_file.close();
    if (!m_file.fileName().isEmpty())
        m_file.remove();
    Q_EMIT downloadFailed(error);
}

void Updater::cancel()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }
}

bool Updater::canInstall() const
{
#if defined(Q_OS_MACOS)
    // A quarantined app run straight from Downloads runs from a read-only
    // copy macOS makes for it; that one can't be replaced.
    const QString bundle = macBundlePath();
    return !bundle.isEmpty() && !bundle.contains(QLatin1String("/AppTranslocation/"))
        && isWritableDir(QFileInfo(bundle).absolutePath()) && isWritableDir(bundle);
#elif defined(Q_OS_WIN)
    return isWritableDir(QCoreApplication::applicationDirPath());
#elif defined(Q_OS_LINUX)
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return !appImage.isEmpty() && isWritableDir(QFileInfo(appImage).absolutePath());
#else
    return false;
#endif
}

bool Updater::install(QString *error)
{
    if (!m_file.exists()) {
        *error = tr("Nothing has been downloaded.");
        return false;
    }
#if defined(Q_OS_MACOS)
    return installMac(error);
#elif defined(Q_OS_WIN)
    return installWindows(error);
#elif defined(Q_OS_LINUX)
    return installAppImage(error);
#else
    *error = tr("Updating isn't supported here.");
    return false;
#endif
}

// Copies the app out of the disk image beside the running one, then a shell
// script swaps the two once NewAmp has quit, putting the old one back if the
// swap fails, and opens the new one. The download never gets the quarantine
// flag (only browsers set it), so the ad hoc signed app opens without a
// Gatekeeper prompt.
bool Updater::installMac(QString *error)
{
    const QString bundle = macBundlePath();
    const QString staging = bundle + QStringLiteral(".update");
    const QString mount = QDir::temp().filePath(QStringLiteral("NewAmp-update/mount"));
    QDir(staging).removeRecursively();
    QDir().mkpath(mount);
    if (!run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("attach"), QStringLiteral("-nobrowse"),
                                                  QStringLiteral("-readonly"), QStringLiteral("-noautoopen"),
                                                  QStringLiteral("-mountpoint"), mount, m_file.fileName()})) {
        *error = tr("Could not open the disk image.");
        return false;
    }
    QString app = mount;
    const QStringList apps = QDir(mount).entryList({QStringLiteral("*.app")}, QDir::Dirs);
    if (!apps.isEmpty())
        app = QDir(mount).filePath(apps.first());
    const bool copied = QFileInfo::exists(app + QStringLiteral("/Contents/MacOS/NewAmp"))
        && run(QStringLiteral("/usr/bin/ditto"), {app, staging});
    run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("detach"), mount, QStringLiteral("-quiet")});
    m_file.remove();
    if (!copied) {
        QDir(staging).removeRecursively();
        *error = tr("Could not copy NewAmp from the disk image.");
        return false;
    }
    const QString script = QStringLiteral(
        "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done\n"
        "rm -rf \"$2.old\"\n"
        "if mv \"$2\" \"$2.old\" && mv \"$3\" \"$2\"; then rm -rf \"$2.old\"\n"
        "else [ -d \"$2\" ] || mv \"$2.old\" \"$2\"; rm -rf \"$3\"; fi\n"
        "open \"$2\"\n");
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"),
                                 {QStringLiteral("-c"), script, QStringLiteral("sh"),
                                  QString::number(QCoreApplication::applicationPid()), bundle, staging})) {
        QDir(staging).removeRecursively();
        *error = tr("Could not start the installer.");
        return false;
    }
    return true;
}

// Unpacks the zip with Windows' own tar, then a batch file waits for NewAmp
// to quit, copies the new files over the old ones and starts it again.
bool Updater::installWindows(QString *error)
{
#ifdef Q_OS_WIN
    const QString staging = QDir::temp().filePath(QStringLiteral("NewAmp-update/files"));
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    const QString tar = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) + QStringLiteral("\\System32\\tar.exe");
    const bool unpacked = run(tar, {QStringLiteral("-xf"), m_file.fileName(), QStringLiteral("-C"), staging})
        && QFileInfo::exists(staging + QStringLiteral("/NewAmp.exe"));
    m_file.remove();
    if (!unpacked) {
        QDir(staging).removeRecursively();
        *error = tr("Could not unpack the update.");
        return false;
    }
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const QString from = QDir::toNativeSeparators(staging);
    const QString to = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    QFile script(QDir::temp().filePath(QStringLiteral("NewAmp-update/install.cmd")));
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = script.errorString();
        return false;
    }
    // ping waits a second; timeout needs a console, which this has none of.
    script.write(QStringLiteral(
                     "@echo off\r\n"
                     ":wait\r\n"
                     "tasklist /NH /FI \"PID eq %1\" | find /I \"NewAmp\" >nul && (ping -n 2 127.0.0.1 >nul & goto wait)\r\n"
                     "robocopy \"%2\" \"%3\" /E /R:10 /W:1 /NFL /NDL /NJH /NJS /NP >nul\r\n"
                     "start \"\" \"%3\\NewAmp.exe\"\r\n"
                     "rmdir /s /q \"%2\"\r\n")
                     .arg(pid, from, to)
                     .toLocal8Bit());
    script.close();
    QProcess helper;
    helper.setProgram(QStringLiteral("cmd.exe"));
    helper.setArguments({QStringLiteral("/c"), QDir::toNativeSeparators(script.fileName())});
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
    if (!helper.startDetached()) {
        *error = tr("Could not start the installer.");
        return false;
    }
    return true;
#else
    *error = tr("Updating isn't supported here.");
    return false;
#endif
}

// Linux lets a running file be replaced, so the new AppImage goes straight
// over the old one; a shell waits for NewAmp to quit and starts it again
// (sooner, the running copy would take the launch as its own).
bool Updater::installAppImage(QString *error)
{
    const QString target = qEnvironmentVariable("APPIMAGE");
    const QString staged = target + QStringLiteral(".update");
    QFile::remove(staged);
    if (!QFile::copy(m_file.fileName(), staged)) {
        *error = tr("Could not write next to %1.").arg(target);
        return false;
    }
    m_file.remove();
    QFile::setPermissions(staged, QFile::permissions(target) | QFile::ExeOwner | QFile::ReadOwner);
    if (std::rename(QFile::encodeName(staged).constData(), QFile::encodeName(target).constData()) != 0) {
        QFile::remove(staged);
        *error = tr("Could not replace %1.").arg(target);
        return false;
    }
    return QProcess::startDetached(QStringLiteral("/bin/sh"),
                                   {QStringLiteral("-c"),
                                    QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$2\""),
                                    QStringLiteral("sh"), QString::number(QCoreApplication::applicationPid()), target});
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    const ParsedVersion x = parseVersion(a);
    const ParsedVersion y = parseVersion(b);
    for (int i = 0; i < qMax(x.core.size(), y.core.size()); ++i) {
        const int p = x.core.value(i), q = y.core.value(i);
        if (p != q)
            return p < q ? -1 : 1;
    }
    // A pre-release comes before its release.
    if (x.prerelease.isEmpty() != y.prerelease.isEmpty())
        return x.prerelease.isEmpty() ? 1 : -1;
    for (int i = 0; i < qMin(x.prerelease.size(), y.prerelease.size()); ++i) {
        bool pNumber, qNumber;
        const int p = x.prerelease.at(i).toInt(&pNumber);
        const int q = y.prerelease.at(i).toInt(&qNumber);
        if (pNumber && qNumber) {
            if (p != q)
                return p < q ? -1 : 1;
        } else if (pNumber != qNumber) {
            return pNumber ? -1 : 1; // numbers sort before words
        } else if (const int c = x.prerelease.at(i).compare(y.prerelease.at(i))) {
            return c < 0 ? -1 : 1;
        }
    }
    return x.prerelease.size() == y.prerelease.size() ? 0 : (x.prerelease.size() < y.prerelease.size() ? -1 : 1);
}

bool Updater::isPrerelease(const QString &version)
{
    return !parseVersion(version).prerelease.isEmpty();
}

UpdateRelease Updater::newestRelease(const QByteArray &json, const QString &current, bool prereleases,
                                     const QString &assetName)
{
    static const QRegularExpression versionTag(QStringLiteral("^v?\\d+(\\.\\d+){0,2}(-[0-9A-Za-z.-]+)?$"));
    UpdateRelease best;
    const QJsonArray releases = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : releases) {
        const QJsonObject release = value.toObject();
        const QString tag = release.value(QLatin1String("tag_name")).toString();
        const bool prerelease = release.value(QLatin1String("prerelease")).toBool() || isPrerelease(tag);
        if (release.value(QLatin1String("draft")).toBool() || (prerelease && !prereleases)
            || !versionTag.match(tag).hasMatch() || compareVersions(tag, current) <= 0
            || (best.isValid() && compareVersions(tag, best.version) <= 0))
            continue;
        UpdateRelease candidate;
        candidate.version = tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? tag.mid(1) : tag;
        candidate.notes = release.value(QLatin1String("body")).toString();
        candidate.page = QUrl(release.value(QLatin1String("html_url")).toString());
        candidate.prerelease = prerelease;
        for (const QJsonValue &a : release.value(QLatin1String("assets")).toArray()) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value(QLatin1String("name")).toString();
            const QUrl url(asset.value(QLatin1String("browser_download_url")).toString());
            if (!isGitHubUrl(url))
                continue;
            if (name == ChecksumsName) {
                candidate.checksums = url;
            } else if (!assetName.isEmpty() && name == assetName) {
                candidate.assetName = name;
                candidate.asset = url;
                const QString digest = asset.value(QLatin1String("digest")).toString();
                if (digest.startsWith(QLatin1String("sha256:")))
                    candidate.sha256 = digest.mid(7).toLatin1().toLower();
            }
        }
        if (!isGitHubUrl(candidate.page))
            candidate.page = QUrl();
        best = candidate;
    }
    return best;
}

QByteArray Updater::checksumFor(const QByteArray &listing, const QString &fileName)
{
    // "<hash>  <name>", or "<hash> *<name>" for binary mode.
    for (const QByteArray &line : listing.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        const qsizetype space = trimmed.indexOf(' ');
        if (space != 64)
            continue;
        QByteArray name = trimmed.mid(space + 1).trimmed();
        if (name.startsWith('*'))
            name.remove(0, 1);
        if (QString::fromUtf8(name) == fileName)
            return trimmed.left(64).toLower();
    }
    return {};
}

QString Updater::platformAssetName()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("NewAmp-macOS.dmg");
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("NewAmp-Windows-x64.zip");
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    // Only the AppImage replaces itself; other Linux builds came from
    // somewhere else (a package, a source build) that does their updates.
    return qEnvironmentVariableIsEmpty("APPIMAGE") ? QString() : QStringLiteral("NewAmp-x86_64.AppImage");
#else
    return {};
#endif
}

bool Updater::isGitHubUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.scheme() == QLatin1String("https")
        && (host == QLatin1String("github.com") || host == QLatin1String("api.github.com")
            || host.endsWith(QLatin1String(".githubusercontent.com")));
}

} // namespace newamp
