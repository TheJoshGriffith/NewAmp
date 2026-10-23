#pragma once

#include "SpotifyApi.h"
#include "core/MediaSource.h"

#include <QDateTime>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class QTcpServer;

namespace newamp::spotify {

class SpotifyConnect;

// Talks to Spotify: sign-in (OAuth with PKCE through a loopback redirect),
// token refresh and Web API requests. Also the MediaSource for Spotify
// tracks, which play through SpotifyConnect rather than NewAmp's decoder.
class SpotifyClient final : public QObject, public MediaSource
{
    Q_OBJECT

public:
    // status is the HTTP status (0 when the request never got an answer).
    using Callback = std::function<void(int status, const QByteArray &body)>;

    // settings must outlive the client; the client id and tokens live there.
    explicit SpotifyClient(QSettings &settings, QObject *parent = nullptr);
    ~SpotifyClient() override;

    QString id() const override { return QString::fromLatin1(SourceId); }
    QString displayName() const override { return QStringLiteral("Spotify"); }
    QUrl streamUrl(const Track &track) const override { return track.location; }
    RemotePlayer *remotePlayer() const override;
    SpotifyConnect *connectPlayer() const { return m_connect; }

    // Each user registers their own app with Spotify (development mode apps
    // take five users), so the client id is a setting.
    QString clientId() const;
    void setClientId(const QString &clientId);

    bool isSignedIn() const { return !m_refreshToken.isEmpty(); }
    // Opens the browser at Spotify's consent page and waits for its redirect.
    void signIn();
    void signOut();

    // A Web API request. path is relative to https://api.spotify.com/v1 or
    // an absolute URL (the "next" link of a page). Tokens are refreshed and
    // rate limits waited out on the way. done runs unless context is gone.
    void request(const QByteArray &verb, const QString &path, const QByteArray &body, QObject *context,
                 const Callback &done);
    // GET that only calls back on success; failures go to status().
    void get(const QString &path, QObject *context, const std::function<void(const QByteArray &json)> &onJson);

    // A failed request's reason, for status lines and track errors.
    static QString errorText(int status, const QByteArray &body);

Q_SIGNALS:
    void signedInChanged(bool signedIn);
    // Human-readable progress and errors for the library's status line.
    void status(const QString &message);

private:
    void finishSignIn(const QString &code, const QString &state, const QString &error);
    void requestTokens(const QByteArray &body);
    void saveTokens(const Tokens &tokens);
    void withToken(QObject *context, const std::function<void(bool ok)> &then);
    void send(const QByteArray &verb, const QString &path, const QByteArray &body, QObject *context,
              const Callback &done, int attempt);

    QSettings &m_settings;
    QNetworkAccessManager *m_network;
    SpotifyConnect *m_connect;
    QTcpServer *m_redirect = nullptr; // listening while a sign-in waits
    QByteArray m_verifier;
    QString m_state;
    QString m_accessToken;
    QDateTime m_expires;
    QString m_refreshToken;
    QList<std::function<void(bool ok)>> m_waiting; // requests held for a refresh
};

} // namespace newamp::spotify
