#pragma once

#include "JellyfinApi.h"
#include "core/MediaSource.h"

#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSettings;

namespace newamp::jellyfin {

class JellyfinEditor;

// Talks to one Jellyfin server: sign-in with a password or an API key, JSON
// requests and playback reports. Also the MediaSource that turns Jellyfin
// tracks into stream URLs, with the server's MP3 conversion as fallback.
class JellyfinClient final : public QObject, public MediaSource
{
    Q_OBJECT

public:
    using JsonCallback = std::function<void(const QByteArray &json)>;
    // httpStatus is 0 when the server didn't answer; error is empty on success.
    using ReplyCallback = std::function<void(int httpStatus, const QByteArray &body, const QString &error)>;
    // Empty error on success.
    using SignInCallback = std::function<void(const QString &error)>;

    // settings must outlive the client; the session and device id live there.
    explicit JellyfinClient(QSettings &settings, QObject *parent = nullptr);

    QString id() const override { return QString::fromLatin1(SourceId); }
    QString displayName() const override { return QStringLiteral("Jellyfin"); }
    QUrl streamUrl(const Track &track) const override;
    QUrl fallbackUrl(const Track &track) const override;
    MetadataEditor *metadataEditor() const override;

    bool isSignedIn() const { return !m_token.isEmpty(); }
    QString serverUrl() const { return m_serverUrl; }
    QString serverId() const { return m_serverId; }
    QString serverName() const { return m_serverName; }
    QString userId() const { return m_userId; }
    QString userName() const { return m_userName; }
    // Signed in with an API key, which an administrator made.
    bool usesApiKey() const;

    // Checks the server, then signs in with the password, or with apiKey
    // when it's set (browsing as the user named userName, or the first).
    // done runs once, with an error to show when it failed.
    void signIn(const QString &serverText, const QString &userName, const QString &password, const QString &apiKey,
                const SignInCallback &done);
    // Drops a sign-in still in progress; its callback won't run.
    void cancelSignIn();
    void signOut();

    // GET a JSON path (with query); the callback runs only on success.
    // Replies are tied to context, so they're dropped with it.
    void get(const QString &path, QObject *context, const JsonCallback &onJson);
    // Fire-and-forget POST of a JSON body, for playback reports.
    void post(const QString &path, const QByteArray &json);
    // Any request, with the outcome either way: for File Info, which has to
    // tell a refusal from a failure. A body is sent as JSON.
    void send(const QByteArray &method, const QString &path, const QByteArray &body, QObject *context,
              const ReplyCallback &done);

    // The item a track plays, or an invalid ref when it isn't from this
    // server or nobody is signed in.
    TrackRef playable(const Track &track) const;

Q_SIGNALS:
    void signedInChanged(bool signedIn);
    // Human-readable progress and errors for the browser's status line.
    void status(const QString &message);

private:
    QNetworkRequest request(const QString &base, const QString &path, const QString &token) const;
    void finishSignIn(const QString &serverUrl, const ServerInfo &server, const Session &session, bool apiKey);

    QSettings &m_settings;
    QNetworkAccessManager *m_network;
    QString m_deviceId;
    QString m_serverUrl;
    QString m_serverId;
    QString m_serverName;
    QString m_userId;
    QString m_userName;
    QString m_token;
    QObject *m_signIn = nullptr; // parent of an in-flight sign-in
    JellyfinEditor *m_editor;
};

} // namespace newamp::jellyfin
