#include "platform/SingleInstance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>

#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace newamp {

namespace {
// The first line of every request, so a stray connection can't add files.
constexpr char Greeting[] = "NEWAMP/1";
} // namespace

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    m_flush.setSingleShot(true);
    m_flush.setInterval(100);
    connect(&m_flush, &QTimer::timeout, this, [this] { Q_EMIT filesOpened(std::exchange(m_pending, {})); });
    // Even with multiple instances allowed, macOS opens files this way.
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

SingleInstance::~SingleInstance()
{
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removeEventFilter(this);
}

QString SingleInstance::defaultKey()
{
    // Hashed, as the name becomes a socket path or pipe name.
    const QByteArray home = QDir::homePath().toUtf8();
    return QStringLiteral("newamp-")
        + QString::fromLatin1(QCryptographicHash::hash(home, QCryptographicHash::Sha1).toHex().left(16));
}

QStringList SingleInstance::pathsForArguments(const QStringList &arguments)
{
    QStringList paths;
    for (const QString &argument : arguments) {
        // Desktop files may pass file:// URLs.
        const QUrl url(argument);
        const QString path = url.isLocalFile() ? url.toLocalFile() : argument;
        if (!path.isEmpty() && !path.startsWith(QLatin1Char('-')))
            paths << QFileInfo(path).absoluteFilePath();
    }
    return paths;
}

bool SingleInstance::sendToRunning(const QStringList &arguments, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(timeoutMs))
        return false;
#ifdef Q_OS_WIN
    // Lets the running copy bring itself to the front, which Windows only
    // allows the app the user just interacted with.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QByteArray request = QByteArray(Greeting) + '\n';
    for (const QString &path : pathsForArguments(arguments))
        request += path.toUtf8() + '\n';
    socket.write(request);
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(timeoutMs);
    return true;
}

bool SingleInstance::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // Nothing answered sendToRunning(), so a leftover socket is from a crash.
    if (!m_server->listen(m_key)) {
        QLocalServer::removeServer(m_key);
        if (!m_server->listen(m_key)) {
            qWarning("NewAmp: can't take files from other launches: %s", qPrintable(m_server->errorString()));
            return false;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                const QList<QByteArray> lines = socket->readAll().split('\n');
                socket->deleteLater();
                if (lines.isEmpty() || lines.first() != Greeting)
                    return;
                QStringList paths;
                for (qsizetype i = 1; i < lines.size(); ++i)
                    if (!lines.at(i).isEmpty())
                        paths << QString::fromUtf8(lines.at(i));
                Q_EMIT filesOpened(paths);
            });
        }
    });
    return true;
}

bool SingleInstance::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FileOpen && watched == QCoreApplication::instance()) {
        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (!path.isEmpty()) {
            m_pending << path;
            m_flush.start();
        }
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace newamp
