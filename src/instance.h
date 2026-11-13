#pragma once

#include <QLocalServer>
#include <QObject>

// Keeps layerling to one running copy per user. A second `layerling` or
// `layerling open <url>` hands its address to the running copy and exits, so
// every window shares one server and one storage, and the MCP bridge's
// "open an editor" lands in the app rather than a web browser.
class Instance : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString initialUrl READ initialUrl CONSTANT)

public:
    explicit Instance(const QString &initialUrl, QObject *parent = nullptr);

    QString initialUrl() const { return m_initialUrl; }

    // Sends `url` (empty for "just show a window") to a running copy.
    // Returns false when there is none, and this process should become it.
    static bool forward(const QString &url, const QString &socketPath = {});

    // Starts listening; false only if the socket cannot be made.
    bool listen(const QString &socketPath = {});

    // Only addresses of the app's own server are opened from outside.
    static QString accepted(const QString &url);

signals:
    void openRequested(const QString &url);

private:
    static QString defaultSocketPath();

    QString m_initialUrl;
    QLocalServer m_server;
};
