#pragma once

#include "PlexApi.h"
#include "core/MediaSource.h"

#include <QHash>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSettings;
class QTimer;

namespace newamp::plex {

class PlexEditor;

// Talks to plex.tv and to Plex Media Servers: PIN sign-in, server discovery,
// JSON requests and playback reports. Also the MediaSource that turns Plex
// tracks into direct-play URLs, with the server's transcoder as fallback.
class PlexClient final : public QObject, public MediaSource
{
    Q_OBJECT

public:
    // A server with the connection that answered.
    struct ServerInfo
    {
        QString name;
        QString machineIdentifier;
        QString uri;
        QString token;
        bool owned = true; // the account's own server, not one shared with it
    };

    using JsonCallback = std::function<void(const QByteArray &json)>;
    // httpStatus is 0 when the server didn't answer; error is empty on success.
    using ReplyCallback = std::function<void(int httpStatus, const QByteArray &body, const QString &error)>;

    // settings must outlive the client; the token and client id live there.
    explicit PlexClient(QSettings &settings, QObject *parent = nullptr);

    QString id() const override { return QString::fromLatin1(SourceId); }
    QString displayName() const override { return QStringLiteral("Plex"); }
    QUrl streamUrl(const Track &track) const override;
    QUrl fallbackUrl(const Track &track) const override;
    MetadataEditor *metadataEditor() const override;

    bool isSignedIn() const { return !m_token.isEmpty(); }
    // Opens the browser for approval and polls until the PIN is claimed.
    void signIn();
    void signOut();

    // Finds the account's servers and the best reachable connection to each.
    void refreshServers();
    QList<ServerInfo> servers() const { return m_servers.values(); }
    // Null when the server isn't connected.
    const ServerInfo *server(const QString &machineIdentifier) const;

    // GET a JSON path (with query) from a server; the callback runs only on
    // success. Replies are tied to context, so they're dropped with it.
    void get(const QString &machineIdentifier, const QString &path, QObject *context, const JsonCallback &onJson);
    // Fire-and-forget request for timeline and scrobble reports.
    void report(const QString &machineIdentifier, const QString &path);
    // Any request, with the outcome either way: for File Info, which has to
    // tell a refusal from a failure.
    void send(const QString &machineIdentifier, const QByteArray &method, const QString &path, QObject *context,
              const ReplyCallback &done);

Q_SIGNALS:
    void signedInChanged(bool signedIn);
    void serversChanged();
    // Human-readable progress and errors for the browser's status line.
    void status(const QString &message);

private:
    QNetworkRequest request(const QUrl &url, const QString &token) const;
    void pollPin(qint64 pinId);
    // Tries each connection in turn; done(true) once one answers.
    void probe(const Server &server, const QList<Connection> &remaining, const std::function<void(bool found)> &done);
    // Finds a working connection again after the current one stopped answering.
    void reconnect(const QString &machineIdentifier, const std::function<void(bool found)> &done);
    void get(const QString &machineIdentifier, const QString &path, QObject *context, const JsonCallback &onJson,
             bool mayReconnect);

    QSettings &m_settings;
    QNetworkAccessManager *m_network;
    QTimer *m_pinTimer;
    QString m_clientId;
    QString m_token;
    qint64 m_pinId = 0;
    int m_pinPolls = 0;
    QHash<QString, ServerInfo> m_servers; // by lower-case machine identifier
    QHash<QString, Server> m_known;       // every server on the account, reachable or not
    int m_generation = 0;                 // drops probes from an older refresh
    PlexEditor *m_editor;
};

} // namespace newamp::plex
