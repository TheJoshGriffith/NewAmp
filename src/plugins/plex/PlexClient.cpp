#include "PlexClient.h"

#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace newamp::plex {

namespace {
const QString PlexTv = QStringLiteral("https://plex.tv/api/v2");
constexpr int PinPollMs = 2000;
constexpr int PinMaxPolls = 150; // five minutes
constexpr int RequestTimeoutMs = 15000;
constexpr int ProbeTimeoutMs = 4000;

// Errors where the server never answered, as opposed to answering badly.
bool isConnectionError(QNetworkReply::NetworkError error)
{
    return error > QNetworkReply::NoError && error <= QNetworkReply::UnknownNetworkError
        && error != QNetworkReply::OperationCanceledError;
}

// Runs done(reply) when the reply finishes, unless context is gone by then.
// The reply is freed either way.
void onFinished(QNetworkReply *reply, QObject *context, std::function<void(QNetworkReply *)> done)
{
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] { done(reply); });
}
} // namespace

PlexClient::PlexClient(QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
    , m_pinTimer(new QTimer(this))
{
    // Plex identifies each install by a stable client id.
    m_clientId = m_settings.value(QStringLiteral("clientId")).toString();
    if (m_clientId.isEmpty()) {
        m_clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QStringLiteral("clientId"), m_clientId);
    }
    m_token = m_settings.value(QStringLiteral("token")).toString();

    m_pinTimer->setSingleShot(true);
    m_pinTimer->setInterval(PinPollMs);
    connect(m_pinTimer, &QTimer::timeout, this, [this] { pollPin(m_pinId); });
}

QNetworkRequest PlexClient::request(const QUrl &url, const QString &token) const
{
    QNetworkRequest r(url);
    r.setTransferTimeout(RequestTimeoutMs);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("X-Plex-Product", "NewAmp");
    r.setRawHeader("X-Plex-Version", NEWAMP_VERSION);
    r.setRawHeader("X-Plex-Client-Identifier", m_clientId.toUtf8());
    r.setRawHeader("X-Plex-Platform", QSysInfo::productType().toUtf8());
    r.setRawHeader("X-Plex-Platform-Version", QSysInfo::productVersion().toUtf8());
    r.setRawHeader("X-Plex-Device-Name", QSysInfo::machineHostName().toUtf8());
    if (!token.isEmpty())
        r.setRawHeader("X-Plex-Token", token.toUtf8());
    return r;
}

void PlexClient::signIn()
{
    if (m_pinId != 0)
        return; // already waiting for approval
    QNetworkReply *reply = m_network->post(request(QUrl(PlexTv + QStringLiteral("/pins?strong=true")), {}),
                                           QByteArray());
    onFinished(reply, this, [this](QNetworkReply *reply) {
        const Pin pin = parsePin(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || pin.id == 0) {
            Q_EMIT status(tr("Could not start Plex sign-in: %1").arg(reply->errorString()));
            return;
        }
        m_pinId = pin.id;
        m_pinPolls = 0;
        QDesktopServices::openUrl(authUrl(m_clientId, pin.code));
        Q_EMIT status(tr("Approve NewAmp in your browser to finish signing in..."));
        m_pinTimer->start();
    });
}

void PlexClient::pollPin(qint64 pinId)
{
    if (pinId == 0 || pinId != m_pinId)
        return;
    if (++m_pinPolls > PinMaxPolls) {
        m_pinId = 0;
        Q_EMIT status(tr("Plex sign-in timed out"));
        return;
    }
    QNetworkReply *reply = m_network->get(request(QUrl(PlexTv + QStringLiteral("/pins/%1").arg(pinId)), {}));
    onFinished(reply, this, [this, pinId](QNetworkReply *reply) {
        if (pinId != m_pinId)
            return; // cancelled meanwhile
        const Pin pin = parsePin(reply->readAll());
        if (pin.authToken.isEmpty()) {
            m_pinTimer->start(); // not approved yet, or a transient error
            return;
        }
        m_pinId = 0;
        m_token = pin.authToken;
        // Stored in plain settings for now; moving it to the system keychain
        // (Keychain, Credential Manager, Secret Service) is future work.
        m_settings.setValue(QStringLiteral("token"), m_token);
        Q_EMIT status(tr("Signed in to Plex"));
        Q_EMIT signedInChanged(true);
        refreshServers();
    });
}

void PlexClient::signOut()
{
    m_pinTimer->stop();
    m_pinId = 0;
    m_token.clear();
    m_settings.remove(QStringLiteral("token"));
    m_servers.clear();
    m_known.clear();
    ++m_generation;
    Q_EMIT signedInChanged(false);
    Q_EMIT serversChanged();
}

void PlexClient::refreshServers()
{
    if (!isSignedIn())
        return;
    const int generation = ++m_generation;
    m_servers.clear();
    Q_EMIT serversChanged();
    Q_EMIT status(tr("Looking for Plex servers..."));
    const QUrl url(PlexTv + QStringLiteral("/resources?includeHttps=1&includeRelay=1"));
    onFinished(m_network->get(request(url, m_token)), this, [this, generation](QNetworkReply *reply) {
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT status(tr("Could not list Plex servers: %1").arg(reply->errorString()));
            return;
        }
        const QList<Server> servers = parseResources(reply->readAll());
        if (servers.isEmpty())
            Q_EMIT status(tr("No Plex servers found on this account"));
        m_known.clear();
        for (const Server &server : servers) {
            m_known.insert(server.machineIdentifier.toLower(), server);
            probe(server, orderedConnections(server), [this](bool found) {
                if (found)
                    Q_EMIT serversChanged();
            });
        }
    });
}

void PlexClient::probe(const Server &server, const QList<Connection> &remaining,
                       const std::function<void(bool found)> &done)
{
    if (remaining.isEmpty()) {
        Q_EMIT status(tr("Could not reach Plex server \"%1\"").arg(server.name));
        done(false);
        return;
    }
    // A local address is useless away from home, so check each in turn.
    const Connection connection = remaining.first();
    QNetworkRequest r = request(QUrl(connection.uri + QStringLiteral("/identity")), server.accessToken);
    r.setTransferTimeout(ProbeTimeoutMs);
    const int generation = m_generation;
    onFinished(m_network->get(r), this, [this, server, remaining, generation, done](QNetworkReply *reply) {
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            probe(server, remaining.mid(1), done);
            return;
        }
        ServerInfo info{server.name, server.machineIdentifier, remaining.first().uri, server.accessToken};
        m_servers.insert(server.machineIdentifier.toLower(), info);
        Q_EMIT status(tr("Connected to %1").arg(server.name));
        done(true);
    });
}

void PlexClient::reconnect(const QString &machineIdentifier, const std::function<void(bool found)> &done)
{
    const QString id = machineIdentifier.toLower();
    const auto known = m_known.constFind(id);
    if (known == m_known.constEnd()) {
        done(false);
        return;
    }
    // Try the other addresses first; the one that just failed goes last.
    const QString failed = m_servers.value(id).uri;
    QList<Connection> connections = orderedConnections(*known);
    std::stable_partition(connections.begin(), connections.end(),
                          [&](const Connection &c) { return c.uri != failed; });
    m_servers.remove(id);
    probe(*known, connections, done);
}

const PlexClient::ServerInfo *PlexClient::server(const QString &machineIdentifier) const
{
    const auto it = m_servers.constFind(machineIdentifier.toLower());
    return it == m_servers.constEnd() ? nullptr : &it.value();
}

QUrl PlexClient::streamUrl(const Track &track) const
{
    const TrackRef ref = parseTrackLocation(track.location);
    const ServerInfo *info = server(ref.machineIdentifier);
    if (!ref.isValid() || !info) {
        qWarning("NewAmp: Plex server for %s is not available", qPrintable(track.location.toString()));
        return {};
    }
    return plex::streamUrl(info->uri, ref.partKey, info->token);
}

QUrl PlexClient::fallbackUrl(const Track &track) const
{
    const TrackRef ref = parseTrackLocation(track.location);
    const ServerInfo *info = server(ref.machineIdentifier);
    if (!ref.isValid() || ref.ratingKey.isEmpty() || !info)
        return {};
    return plex::transcodeUrl(info->uri, ref.ratingKey, info->token, m_clientId,
                              QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void PlexClient::get(const QString &machineIdentifier, const QString &path, QObject *context,
                     const JsonCallback &onJson)
{
    get(machineIdentifier, path, context, onJson, true);
}

void PlexClient::get(const QString &machineIdentifier, const QString &path, QObject *context,
                     const JsonCallback &onJson, bool mayReconnect)
{
    const ServerInfo *info = server(machineIdentifier);
    if (!info) {
        Q_EMIT status(tr("Plex server is not available"));
        return;
    }
    QNetworkReply *reply = m_network->get(request(QUrl(info->uri + path), info->token));
    QPointer<QObject> guard(context);
    onFinished(reply, context, [this, machineIdentifier, path, guard, onJson, mayReconnect](QNetworkReply *reply) {
        const QNetworkReply::NetworkError error = reply->error();
        if (error == QNetworkReply::NoError) {
            onJson(reply->readAll());
            return;
        }
        // A server's address can stop working (another network, a new IP, or
        // macOS withholding local network access), so find another and retry.
        if (mayReconnect && isConnectionError(error)) {
            Q_EMIT status(tr("Reconnecting to Plex..."));
            reconnect(machineIdentifier, [this, machineIdentifier, path, guard, onJson](bool found) {
                if (found && guard)
                    get(machineIdentifier, path, guard, onJson, false);
            });
            return;
        }
        Q_EMIT status(tr("Plex request failed: %1").arg(reply->errorString()));
    });
}

void PlexClient::report(const QString &machineIdentifier, const QString &path)
{
    if (const ServerInfo *info = server(machineIdentifier))
        onFinished(m_network->get(request(QUrl(info->uri + path), info->token)), this, [](QNetworkReply *) {});
}

} // namespace newamp::plex
