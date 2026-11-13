#include "backend.h"

#include "paths.h"

#include <QFileInfo>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <csignal>
#include <sys/prctl.h>

Backend::Backend(const QString &runtime, const QString &dataDirectory, const QString &node, QObject *parent)
    : QObject(parent), m_runtime(runtime), m_dataDirectory(dataDirectory), m_node(node) {
    m_probe.setInterval(250);
    connect(&m_probe, &QTimer::timeout, this, &Backend::probe);

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    // The server must not outlive the app, even when the app is killed
    // without a chance to stop it: the kernel ends it with the app.
    m_process.setChildProcessModifier([] {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
    connect(&m_process, &QProcess::readyRead, this, [this] {
        m_log += QString::fromUtf8(m_process.readAll());
        if (m_log.size() > 20000)
            m_log = m_log.right(8000);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Node.js could not be started: %1").arg(m_process.errorString()));
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_state != Failed)
            fail(tr("The layerling server stopped (exit code %1).").arg(code));
    });
}

Backend::~Backend() { stop(); }

QString Backend::url() const { return Paths::baseUrl() + "/"; }

void Backend::start() {
    stop();
    m_error.clear();
    m_log.clear();
    m_checkedForOther = false;
    m_state = Starting;
    emit stateChanged();
    m_started.start();
    // Ask first whether a layerling server already answers (another window),
    // and only start our own when nothing does.
    probe();
    m_probe.start();
}

void Backend::stop() {
    m_probe.stop();
    if (m_process.state() == QProcess::NotRunning)
        return;
    // Silence the "server stopped" report: this stop is on purpose.
    m_state = Failed;
    m_process.terminate();
    if (!m_process.waitForFinished(3000)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void Backend::probe() {
    if (m_probing || m_state != Starting)
        return;
    if (m_started.elapsed() > startTimeoutMs) {
        fail(tr("The layerling server did not answer within %n seconds.", "", startTimeoutMs / 1000));
        return;
    }
    m_probing = true;
    QNetworkRequest request{QUrl(url())};
    request.setTransferTimeout(2000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_probing = false;
        if (m_state != Starting)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool answered = reply->error() != QNetworkReply::ConnectionRefusedError &&
                              reply->error() != QNetworkReply::TimeoutError &&
                              reply->error() != QNetworkReply::OperationCanceledError && status != 0;
        const bool isLayerling = status == 200 && reply->rawHeader("x-powered-by").contains("Next.js");

        if (isLayerling) {
            m_probe.stop();
            m_state = Ready;
            emit stateChanged();
            return;
        }
        if (!m_checkedForOther) {
            m_checkedForOther = true;
            if (answered) {
                fail(tr("Another program is using port %1.").arg(Paths::port));
                return;
            }
            // Nothing there yet: start our own server and keep probing.
            if (m_node.isEmpty()) {
                fail(tr("Node.js was not found. Install the nodejs package."));
                return;
            }
            QString error;
            const QString server = Paths::prepareServerTree(m_runtime, m_dataDirectory, &error);
            if (server.isEmpty()) {
                fail(error == "missing" ? tr("The layerling server files were not found in %1.").arg(m_runtime)
                                        : tr("The layerling data folder could not be prepared: %1").arg(error));
                return;
            }
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("NODE_ENV", "production");
            environment.insert("NEXT_TELEMETRY_DISABLED", "1");
            environment.insert("HOSTNAME", "127.0.0.1");
            environment.insert("PORT", QString::number(Paths::port));
            // The bridge stays closed in production builds unless switched on
            // with a token; `layerling mcp` hands the same token to the client.
            environment.insert("LAYERLING_MCP_REMOTE", "true");
            environment.insert("LAYERLING_MCP_TOKEN", Paths::mcpToken(m_dataDirectory));
            m_process.setProcessEnvironment(environment);
            m_process.setWorkingDirectory(QFileInfo(server).absolutePath());
            // Keep the link tree's paths, so the server's own folder (and the
            // preview pictures under it) is the user's, not the package's.
            m_process.start(m_node, {"--preserve-symlinks", "--preserve-symlinks-main", server});
        }
    });
}

void Backend::fail(const QString &message) {
    m_probe.stop();
    m_state = Failed;
    m_error = message;
    emit stateChanged();
}
