#include "JellyfinClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>

namespace newamp::jellyfin {

namespace {
constexpr int RequestTimeoutMs = 15000;

// Runs done(reply) when the reply finishes, unless context is gone by then.
// The reply is freed either way.
void onFinished(QNetworkReply *reply, QObject *context, std::function<void(QNetworkReply *)> done)
{
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] { done(reply); });
}

int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

const QString Keys[] = {QStringLiteral("serverUrl"), QStringLiteral("serverId"), QStringLiteral("serverName"),
                        QStringLiteral("userId"),    QStringLiteral("userName"), QStringLiteral("token"),
                        QStringLiteral("apiKey")};
} // namespace

JellyfinClient::JellyfinClient(QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
{
    // Jellyfin lists each install as a device by a stable id.
    m_deviceId = m_settings.value(QStringLiteral("deviceId")).toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QStringLiteral("deviceId"), m_deviceId);
    }
    m_serverUrl = m_settings.value(QStringLiteral("serverUrl")).toString();
    m_serverId = m_settings.value(QStringLiteral("serverId")).toString();
    m_serverName = m_settings.value(QStringLiteral("serverName")).toString();
    m_userId = m_settings.value(QStringLiteral("userId")).toString();
    m_userName = m_settings.value(QStringLiteral("userName")).toString();
    m_token = m_settings.value(QStringLiteral("token")).toString();
    if (m_serverUrl.isEmpty() || m_userId.isEmpty())
        m_token.clear();
}

QNetworkRequest JellyfinClient::request(const QString &base, const QString &path, const QString &token) const
{
    QNetworkRequest r(QUrl(base + path, QUrl::StrictMode));
    r.setTransferTimeout(RequestTimeoutMs);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("Authorization", authorizationHeader(QSysInfo::machineHostName(), m_deviceId, token));
    return r;
}

void JellyfinClient::signIn(const QString &serverText, const QString &userName, const QString &password,
                            const QString &apiKey, const SignInCallback &done)
{
    const QString url = normalizeServerUrl(serverText);
    if (url.isEmpty()) {
        done(tr("Enter the server's address, such as http://192.168.1.20:8096"));
        return;
    }
    // A newer attempt replaces one still running.
    delete m_signIn;
    m_signIn = new QObject(this);
    QObject *attempt = m_signIn;
    auto finish = [this, attempt, done](const QString &error) {
        if (m_signIn == attempt) {
            m_signIn = nullptr;
            attempt->deleteLater();
        }
        done(error);
    };

    // The public info answers without signing in, so a wrong address is
    // told apart from a wrong password.
    QNetworkReply *reply = m_network->get(request(url, QStringLiteral("/System/Info/Public"), {}));
    onFinished(reply, attempt, [=](QNetworkReply *reply) {
        const ServerInfo server = parseServerInfo(reply->readAll());
        if (reply->error() != QNetworkReply::NoError && server.id.isEmpty()) {
            finish(tr("Could not reach %1: %2").arg(url, reply->errorString()));
            return;
        }
        if (server.id.isEmpty()) {
            finish(tr("%1 doesn't look like a Jellyfin server").arg(url));
            return;
        }

        if (!apiKey.trimmed().isEmpty()) {
            const QString key = apiKey.trimmed();
            onFinished(m_network->get(request(url, QStringLiteral("/Users"), key)), attempt, [=](QNetworkReply *reply) {
                if (reply->error() != QNetworkReply::NoError) {
                    finish(httpStatus(reply) == 401 || httpStatus(reply) == 403
                             ? tr("The server didn't accept that API key")
                             : tr("Could not list users: %1").arg(reply->errorString()));
                    return;
                }
                const QList<User> users = parseUsers(reply->readAll());
                const User *user = pickUser(users, userName);
                if (!user) {
                    finish(tr("The server has no users to browse as"));
                    return;
                }
                finishSignIn(url, server, {key, user->id, user->name, server.id}, true);
                finish({});
            });
            return;
        }

        QNetworkRequest r = request(url, QStringLiteral("/Users/AuthenticateByName"), {});
        r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        onFinished(m_network->post(r, authenticateBody(userName.trimmed(), password)), attempt,
                   [=](QNetworkReply *reply) {
                       const Session session = parseAuthentication(reply->readAll());
                       if (reply->error() != QNetworkReply::NoError || session.token.isEmpty()) {
                           finish(httpStatus(reply) == 401 ? tr("Wrong user name or password")
                                                         : tr("Could not sign in: %1").arg(reply->errorString()));
                           return;
                       }
                       finishSignIn(url, server, session, false);
                       finish({});
                   });
    });
}

void JellyfinClient::finishSignIn(const QString &serverUrl, const ServerInfo &server, const Session &session,
                                  bool apiKey)
{
    m_serverUrl = serverUrl;
    m_serverId = server.id;
    m_serverName = server.name;
    m_userId = session.userId;
    m_userName = session.userName;
    m_token = session.token;
    // Stored in plain settings for now, like the Plex token; moving both to
    // the system keychain is future work.
    m_settings.setValue(QStringLiteral("serverUrl"), m_serverUrl);
    m_settings.setValue(QStringLiteral("serverId"), m_serverId);
    m_settings.setValue(QStringLiteral("serverName"), m_serverName);
    m_settings.setValue(QStringLiteral("userId"), m_userId);
    m_settings.setValue(QStringLiteral("userName"), m_userName);
    m_settings.setValue(QStringLiteral("token"), m_token);
    m_settings.setValue(QStringLiteral("apiKey"), apiKey);
    Q_EMIT status(tr("Signed in to %1 as %2").arg(m_serverName, m_userName));
    Q_EMIT signedInChanged(true);
}

void JellyfinClient::cancelSignIn()
{
    delete m_signIn;
    m_signIn = nullptr;
}

void JellyfinClient::signOut()
{
    cancelSignIn();
    // Ends the session on the server too; an API key stays valid, since the
    // admin made it and may use it elsewhere.
    if (isSignedIn() && !m_settings.value(QStringLiteral("apiKey")).toBool())
        post(QStringLiteral("/Sessions/Logout"), {});
    m_token.clear();
    m_userId.clear();
    m_userName.clear();
    for (const QString &key : Keys) {
        if (key != QLatin1String("serverUrl"))
            m_settings.remove(key); // the address stays to fill in next time
    }
    Q_EMIT status({});
    Q_EMIT signedInChanged(false);
}

TrackRef JellyfinClient::playable(const Track &track) const
{
    const TrackRef ref = parseTrackLocation(track.location);
    if (!ref.isValid() || !isSignedIn() || ref.serverId.compare(m_serverId, Qt::CaseInsensitive) != 0)
        return {};
    return ref;
}

QUrl JellyfinClient::streamUrl(const Track &track) const
{
    const TrackRef ref = playable(track);
    if (!ref.isValid()) {
        qWarning("NewAmp: %s is not from the signed-in Jellyfin server", qPrintable(track.location.toString()));
        return {};
    }
    return jellyfin::streamUrl(m_serverUrl, ref, m_token, m_deviceId);
}

QUrl JellyfinClient::fallbackUrl(const Track &track) const
{
    const TrackRef ref = playable(track);
    if (!ref.isValid())
        return {};
    return transcodeUrl(m_serverUrl, ref, m_token, m_deviceId);
}

void JellyfinClient::get(const QString &path, QObject *context, const JsonCallback &onJson)
{
    if (!isSignedIn()) {
        Q_EMIT status(tr("Not signed in to Jellyfin"));
        return;
    }
    QNetworkReply *reply = m_network->get(request(m_serverUrl, path, m_token));
    onFinished(reply, context, [this, onJson](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            onJson(reply->readAll());
            return;
        }
        if (httpStatus(reply) == 401)
            Q_EMIT status(tr("Jellyfin no longer accepts this sign-in. Right-click to sign in again."));
        else
            Q_EMIT status(tr("Jellyfin request failed: %1").arg(reply->errorString()));
    });
}

void JellyfinClient::post(const QString &path, const QByteArray &json)
{
    if (!isSignedIn())
        return;
    QNetworkRequest r = request(m_serverUrl, path, m_token);
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    onFinished(m_network->post(r, json), this, [](QNetworkReply *) {});
}

} // namespace newamp::jellyfin
