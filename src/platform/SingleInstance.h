#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QLocalServer;

namespace newamp {

// Keeps NewAmp to one running copy. A second launch hands its files to the
// first over a local socket and exits, so files opened from Explorer or a
// file manager land in the player that's already open. macOS keeps one copy
// itself and sends file open events instead; those arrive here too.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // Copies with the same key share one instance; tests pass their own.
    explicit SingleInstance(const QString &key = defaultKey(), QObject *parent = nullptr);
    ~SingleInstance() override;

    // Gives `arguments` to the running copy. False when none is running.
    bool sendToRunning(const QStringList &arguments, int timeoutMs = 1000);
    // Starts taking later launches' files.
    bool listen();

    // Command line arguments (paths or file URLs) as absolute paths.
    static QStringList pathsForArguments(const QStringList &arguments);
    // Per user, so people sharing a computer each get their own player.
    static QString defaultKey();

Q_SIGNALS:
    // Files to play. Empty when NewAmp was launched again without any: the
    // player should just come to the front.
    void filesOpened(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
    // macOS sends one event per file; they're gathered into one request.
    QStringList m_pending;
    QTimer m_flush;
};

} // namespace newamp
