#include "SpotifyClient.h"

#include "SpotifyConnect.h"

#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#ifndef NEWAMP_SPOTIFY_CLIENT_ID
#define NEWAMP_SPOTIFY_CLIENT_ID ""
#endif

namespace newamp::spotify {

namespace {
const QString Api = QStringLiteral("https://api.spotify.com/v1");
const QUrl TokenUrl(QStringLiteral("https://accounts.spotify.com/api/token"));
constexpr int RequestTimeoutMs = 15000;
constexpr int SignInTimeoutMs = 5 * 60 * 1000;
constexpr int MaxRetryAfterS = 30;

const QByteArray RedirectPage = "<!doctype html><meta charset=utf-8><title>NewAmp</title>"
                                "<body style=\"font:16px sans-serif;margin:3em\">"
                                "<p>%1</p><p>You can close this tab and go back to NewAmp.</p></body>";
} // namespace

SpotifyClient::SpotifyClient(QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
    , m_connect(new SpotifyConnect(*this, settings, this))
    , m_api(Api)
    , m_tokenUrl(TokenUrl)
{
    m_refreshToken = m_settings.value(QStringLiteral("refreshToken")).toString();
}

SpotifyClient::~SpotifyClient() = default;

RemotePlayer *SpotifyClient::remotePlayer() const
{
    return m_connect;
}

QString SpotifyClient::clientId() const
{
    return m_settings.value(QStringLiteral("clientId"), QStringLiteral(NEWAMP_SPOTIFY_CLIENT_ID)).toString().trimmed();
}

void SpotifyClient::setClientId(const QString &clientId)
{
    if (clientId.trimmed() == this->clientId())
        return;
    // Tokens belong to the app that issued them.
    signOut();
    m_settings.setValue(QStringLiteral("clientId"), clientId.trimmed());
}

void SpotifyClient::setEndpoints(const QString &api, const QUrl &tokenUrl)
{
    m_api = api;
    m_tokenUrl = tokenUrl;
}

void SpotifyClient::signIn()
{
    if (clientId().isEmpty()) {
        Q_EMIT status(tr("Set your Spotify client ID first (right-click here)."));
        return;
    }
    if (!m_redirect) {
        m_redirect = new QTcpServer(this);
        if (!m_redirect->listen(QHostAddress::LocalHost, RedirectPort)) {
            Q_EMIT status(tr("Can't listen on port %1 for Spotify's sign-in: %2")
                              .arg(RedirectPort)
                              .arg(m_redirect->errorString()));
            delete std::exchange(m_redirect, nullptr);
            return;
        }
        connect(m_redirect, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_redirect ? m_redirect->nextPendingConnection() : nullptr) {
                // Outlive the server, which closes as soon as the code arrives,
                // so the page still reaches the browser.
                socket->setParent(this);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const QByteArray head = socket->peek(8192);
                    if (!head.contains("\r\n\r\n") && head.size() < 8192)
                        return; // wait for the whole request
                    // "GET /callback?code=...&state=... HTTP/1.1"
                    const QList<QByteArray> line = head.left(head.indexOf("\r\n")).split(' ');
                    const QUrl target(QString::fromLatin1(line.value(1)));
                    QString message = tr("Nothing to do here.");
                    const bool callback = target.path() == QLatin1String("/callback");
                    if (callback) {
                        const QUrlQuery query(target);
                        message = query.hasQueryItem(QStringLiteral("code"))
                                      ? tr("NewAmp is signed in to Spotify.")
                                      : tr("Spotify sign-in was cancelled.");
                        finishSignIn(query.queryItemValue(QStringLiteral("code")),
                                     query.queryItemValue(QStringLiteral("state")),
                                     query.queryItemValue(QStringLiteral("error")));
                    }
                    const QByteArray page = QString::fromLatin1(RedirectPage).arg(message).toUtf8();
                    socket->write((callback ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n")
                                  + QByteArray("Content-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
                                               "Content-Length: ")
                                  + QByteArray::number(page.size()) + "\r\n\r\n" + page);
                    socket->disconnectFromHost();
                });
            }
        });
        // Give up eventually, so the port isn't held forever.
        QTimer::singleShot(SignInTimeoutMs, m_redirect, [this] {
            Q_EMIT status(tr("Spotify sign-in timed out."));
            m_redirect->deleteLater();
            m_redirect = nullptr;
        });
    }
    m_verifier = makeCodeVerifier();
    m_state = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Q_EMIT status(tr("Waiting for you to approve NewAmp in your browser..."));
    QDesktopServices::openUrl(authorizeUrl(clientId(), codeChallenge(m_verifier), m_state));
}

void SpotifyClient::finishSignIn(const QString &code, const QString &state, const QString &error)
{
    if (m_redirect) {
        m_redirect->close();
        m_redirect->deleteLater();
        m_redirect = nullptr;
    }
    if (state != m_state || m_state.isEmpty())
        return; // not the request we made
    m_state.clear();
    if (code.isEmpty()) {
        Q_EMIT status(error == QLatin1String("access_denied") ? tr("Spotify sign-in was cancelled.")
                                                               : tr("Spotify sign-in failed: %1").arg(error));
        return;
    }
    requestTokens(tokenRequestBody(clientId(), code, std::exchange(m_verifier, {})));
}

void SpotifyClient::requestTokens(const QByteArray &body)
{
    QNetworkRequest r(m_tokenUrl);
    r.setTransferTimeout(RequestTimeoutMs);
    r.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_network->post(r, body);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray json = reply->readAll();
        const Tokens tokens = parseTokens(json);
        const bool wasSignedIn = isSignedIn();
        if (tokens.isValid()) {
            saveTokens(tokens);
        } else if (code == 400) {
            // The refresh token was revoked or belongs to another app.
            Q_EMIT status(tr("Spotify signed NewAmp out: %1").arg(errorText(code, json)));
            signOut();
        } else {
            Q_EMIT status(tr("Spotify sign-in failed: %1").arg(errorText(code, json)));
        }
        const bool ok = tokens.isValid();
        for (const auto &then : std::exchange(m_waiting, {}))
            then(ok);
        if (ok && !wasSignedIn) {
            Q_EMIT status(QString());
            Q_EMIT signedInChanged(true);
        }
    });
}

void SpotifyClient::saveTokens(const Tokens &tokens)
{
    m_accessToken = tokens.accessToken;
    // Refresh a minute early rather than have a request bounce.
    m_expires = QDateTime::currentDateTimeUtc().addSecs(qMax(0, tokens.expiresIn - 60));
    if (!tokens.refreshToken.isEmpty()) {
        m_refreshToken = tokens.refreshToken;
        m_settings.setValue(QStringLiteral("refreshToken"), m_refreshToken);
    }
}

void SpotifyClient::signOut()
{
    const bool was = isSignedIn();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_settings.remove(QStringLiteral("refreshToken"));
    if (was)
        Q_EMIT signedInChanged(false);
}

void SpotifyClient::withToken(QObject *context, const std::function<void(bool ok)> &then)
{
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_expires) {
        then(true);
        return;
    }
    if (m_refreshToken.isEmpty()) {
        then(false);
        return;
    }
    QPointer<QObject> guard(context);
    m_waiting.append([guard, then](bool ok) {
        if (guard)
            then(ok);
    });
    if (m_waiting.size() == 1)
        requestTokens(refreshRequestBody(clientId(), m_refreshToken));
}

void SpotifyClient::request(const QByteArray &verb, const QString &path, const QByteArray &body, QObject *context,
                            const Callback &done)
{
    send(verb, path, body, context, done, 0);
}

void SpotifyClient::send(const QByteArray &verb, const QString &path, const QByteArray &body, QObject *context,
                         const Callback &done, int attempt)
{
    withToken(context, [=](bool ok) {
        if (!ok) {
            done(401, QByteArray());
            return;
        }
        QNetworkRequest r(QUrl(path.startsWith(QLatin1String("http")) ? path : m_api + path));
        r.setTransferTimeout(RequestTimeoutMs);
        r.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
        if (!body.isEmpty())
            r.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        QNetworkReply *reply = m_network->sendCustomRequest(r, verb, body);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        connect(reply, &QNetworkReply::finished, context, [=] {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 401 && attempt == 0) {
                m_accessToken.clear(); // expired early or revoked; refresh and retry once
                send(verb, path, body, context, done, attempt + 1);
                return;
            }
            if (code == 429 && attempt < 2) {
                const int wait = qBound(1, reply->rawHeader("Retry-After").toInt(), MaxRetryAfterS);
                QTimer::singleShot(wait * 1000, context,
                                   [=] { send(verb, path, body, context, done, attempt + 1); });
                return;
            }
            const QByteArray answer = reply->readAll();
            if (debugEnabled())
                qInfo("Spotify %s %s %s-> %d %s %s", verb.constData(), qPrintable(path), body.constData(), code,
                      code == 0 ? qPrintable(reply->errorString()) : "", answer.left(400).simplified().constData());
            done(code, answer);
        });
    });
}

void SpotifyClient::get(const QString &path, QObject *context,
                        const std::function<void(const QByteArray &json)> &onJson)
{
    request("GET", path, {}, context, [this, onJson](int code, const QByteArray &body) {
        if (code >= 200 && code < 300)
            onJson(body);
        else
            Q_EMIT status(errorText(code, body));
    });
}

bool SpotifyClient::debugEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("NEWAMP_SPOTIFY_DEBUG");
    return enabled;
}

QString SpotifyClient::errorText(int status, const QByteArray &body)
{
    if (status == 0)
        return tr("Spotify didn't answer.");
    if (status == 401)
        return tr("Not signed in to Spotify.");
    const ApiError error = parseError(body);
    if (error.reason == QLatin1String("PREMIUM_REQUIRED"))
        return tr("Playing through Spotify needs a Premium account.");
    if (error.reason == QLatin1String("NO_ACTIVE_DEVICE"))
        return tr("No Spotify device is open. Start Spotify on this computer or your phone, then play again.");
    if (status == 403 && error.message.contains(QLatin1String("not be registered"), Qt::CaseInsensitive))
        return tr("This Spotify account isn't on your app's user list in the Spotify developer dashboard.");
    const QString message = error.message.isEmpty() ? tr("error %1").arg(status) : error.message;
    return tr("Spotify: %1").arg(message);
}

} // namespace newamp::spotify
