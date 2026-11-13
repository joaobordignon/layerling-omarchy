#include "files.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickWebEngineDownloadRequest>
#include <QQuickWebEngineProfile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {

// Waits for one portal Request's Response signal, then removes itself.
class PortalResponse : public QObject {
    Q_OBJECT
public:
    PortalResponse(PortalFilePicker::Callback callback, QObject *parent)
        : QObject(parent), m_callback(std::move(callback)) {}

    void finish(bool ok, const QStringList &paths, const QString &error) {
        if (m_done)
            return;
        m_done = true;
        m_callback(ok, paths, error);
        deleteLater();
    }

public slots:
    void onResponse(uint response, const QVariantMap &results) {
        // 0 is success, 1 is the user cancelling, anything else a failure.
        if (response == 0)
            finish(true, PortalFilePicker::pathsFromResults(results), {});
        else
            finish(false, {}, response == 1 ? QString() : QStringLiteral("the file chooser failed"));
    }

private:
    PortalFilePicker::Callback m_callback;
    bool m_done = false;
};

}

QStringList PortalFilePicker::pathsFromResults(const QVariantMap &results) {
    QStringList uris;
    const QVariant value = results.value("uris");
    if (value.canConvert<QDBusArgument>())
        uris = qdbus_cast<QStringList>(value.value<QDBusArgument>());
    else
        uris = value.toStringList();
    QStringList paths;
    for (const QString &uri : uris) {
        const QUrl url(uri);
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    return paths;
}

void PortalFilePicker::openFiles(const QString &title, bool multiple, Callback callback) {
    request("OpenFile", title, {{"multiple", multiple}}, std::move(callback));
}

void PortalFilePicker::saveFile(const QString &title, const QString &suggestedName, const QString &folder,
                                Callback callback) {
    QVariantMap options{{"current_name", suggestedName}};
    if (!folder.isEmpty())
        options.insert("current_folder", QFile::encodeName(folder) + '\0');
    request("SaveFile", title, options, std::move(callback));
}

void PortalFilePicker::request(const QString &method, const QString &title, QVariantMap options,
                               Callback callback) {
    auto bus = QDBusConnection::sessionBus();
    auto *response = new PortalResponse(std::move(callback), this);
    if (!bus.isConnected()) {
        response->finish(false, {}, QStringLiteral("no session bus"));
        return;
    }

    // Subscribe to the Response before calling, at the path the portal will
    // use for our token, so a fast answer cannot slip past.
    const QString token = "layerling" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect("org.freedesktop.portal.Desktop", path, "org.freedesktop.portal.Request", "Response", response,
                SLOT(onResponse(uint, QVariantMap)));
    options.insert("handle_token", token);

    auto message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.FileChooser", method);
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), response);
    connect(watcher, &QDBusPendingCallWatcher::finished, response, [response, watcher] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError())
            response->finish(false, {}, reply.error().message());
        watcher->deleteLater();
    });
}

Files::Files(QQuickWebEngineProfile *profile, const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_staging(dataDirectory + "/exports") {
    // Whatever an earlier run left half-way is of no use now.
    QDir(m_staging).removeRecursively();
    connect(profile, &QQuickWebEngineProfile::downloadRequested, this, &Files::downloadRequested);
}

void Files::tell(const QString &message) {
    m_notice = message;
    emit noticeChanged();
}

void Files::pickForPage(QObject *request, bool multiple) {
    QPointer<QObject> page(request);
    m_picker.openFiles(tr("Open"), multiple, [this, page](bool ok, const QStringList &paths, const QString &error) {
        if (!page)
            return;
        if (ok && !paths.isEmpty())
            QMetaObject::invokeMethod(page, "dialogAccept", Q_ARG(QStringList, paths));
        else
            QMetaObject::invokeMethod(page, "dialogReject");
        if (!error.isEmpty())
            tell(tr("The file chooser is not available: %1").arg(error));
    });
}

void Files::downloadRequested(QQuickWebEngineDownloadRequest *download) {
    // A download must be accepted while the signal runs, before anyone could
    // have picked a folder, so it goes to a private folder first.
    const QString folder = m_staging + "/" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(folder);
    download->setDownloadDirectory(folder);
    download->setDownloadFileName(download->suggestedFileName());
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [this, download, folder] {
        if (download->state() == QWebEngineDownloadRequest::DownloadCompleted)
            downloadFinished(download, folder + "/" + download->downloadFileName());
        else
            QDir(folder).removeRecursively();
    });
    download->accept();
}

void Files::downloadFinished(QQuickWebEngineDownloadRequest *download, const QString &path) {
    const QString name = download->downloadFileName();
    const QString folder = m_lastFolder.isEmpty()
                               ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                               : m_lastFolder;
    m_picker.saveFile(tr("Save %1").arg(name), name, folder,
                      [this, path, name, folder](bool ok, const QStringList &paths, const QString &error) {
        QString target;
        if (ok && !paths.isEmpty())
            target = paths.first();
        else if (!error.isEmpty())
            // Without a file chooser the export still must not be lost.
            target = QDir(folder).filePath(name);

        if (!target.isEmpty()) {
            QString saveError;
            if (saveCopy(path, target, &saveError)) {
                m_lastFolder = QFileInfo(target).absolutePath();
                tell(error.isEmpty() ? tr("Saved %1").arg(QFileInfo(target).fileName())
                                     : tr("The file chooser is not available, so %1 was saved to %2.")
                                           .arg(name, QFileInfo(target).absolutePath()));
            } else {
                tell(tr("Could not save %1: %2").arg(name, saveError));
            }
        }
        QDir(QFileInfo(path).absolutePath()).removeRecursively();
    });
}

bool Files::saveCopy(const QString &source, const QString &target, QString *error) {
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = in.errorString();
        return false;
    }
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(1 << 20);
        if (out.write(chunk) != chunk.size()) {
            *error = out.errorString();
            out.cancelWriting();
            return false;
        }
    }
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}

#include "files.moc"
