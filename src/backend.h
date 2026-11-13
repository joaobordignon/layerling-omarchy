#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Runs layerling's Node server on 127.0.0.1 for the window to show, with the
// MCP bridge switched on behind a token so an AI client can drive the editor.
// If a server already answers on the port (a second window), it is reused.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString log READ log NOTIFY stateChanged)
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)

public:
    enum State { Starting, Ready, Failed };
    Q_ENUM(State)

    explicit Backend(const QString &runtime, const QString &dataDirectory, const QString &node,
                     QObject *parent = nullptr);
    ~Backend() override;

    State state() const { return m_state; }
    QString error() const { return m_error; }
    QString log() const { return m_log.right(4000); }
    QString url() const;
    QString dataDirectory() const { return m_dataDirectory; }

    // How long the server gets to answer before the window says so.
    static constexpr int startTimeoutMs = 60000;

public slots:
    void start();
    void stop();

signals:
    void stateChanged();

private:
    void probe();
    void fail(const QString &message);

    QString m_runtime, m_dataDirectory, m_node;
    State m_state = Starting;
    QString m_error, m_log;
    QProcess m_process;
    QNetworkAccessManager m_network;
    QTimer m_probe;
    QElapsedTimer m_started;
    bool m_probing = false;
    bool m_checkedForOther = false;
};
