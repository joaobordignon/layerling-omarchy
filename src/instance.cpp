#include "instance.h"

#include "paths.h"

#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QUrl>

Instance::Instance(const QString &initialUrl, QObject *parent)
    : QObject(parent), m_initialUrl(accepted(initialUrl)) {}

QString Instance::defaultSocketPath() {
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    return runtime + "/layerling.sock";
}

QString Instance::accepted(const QString &url) {
    const QString base = Paths::baseUrl() + "/";
    const QUrl parsed(url);
    return parsed.isValid() && url.startsWith(base) ? parsed.toString() : QString();
}

bool Instance::forward(const QString &url, const QString &socketPath) {
    QLocalSocket socket;
    socket.connectToServer(socketPath.isEmpty() ? defaultSocketPath() : socketPath);
    if (!socket.waitForConnected(500))
        return false;
    socket.write(url.toUtf8() + '\n');
    socket.flush();
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

bool Instance::listen(const QString &socketPath) {
    const QString path = socketPath.isEmpty() ? defaultSocketPath() : socketPath;
    // A socket left by a copy that crashed would block the name.
    QLocalServer::removeServer(path);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path))
        return false;
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    const QString line = QString::fromUtf8(socket->readLine()).trimmed();
                    // Anything that is not one of our own pages just shows a window.
                    emit openRequested(accepted(line));
                }
            });
        }
    });
    return true;
}
