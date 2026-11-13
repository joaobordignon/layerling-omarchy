#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>

class QQuickWebEngineDownloadRequest;
class QQuickWebEngineProfile;

// Asks xdg-desktop-portal's FileChooser for files, like every other Omarchy
// app, rather than drawing Qt's own dialog. Each call gets a fresh request
// object on the bus; the answer arrives as its Response signal.
class PortalFilePicker : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(bool ok, const QStringList &paths, const QString &error)>;

    explicit PortalFilePicker(QObject *parent = nullptr) : QObject(parent) {}

    void openFiles(const QString &title, bool multiple, Callback callback);
    void saveFile(const QString &title, const QString &suggestedName, const QString &folder, Callback callback);

    // The paths in a FileChooser response, decoded from file:// URIs.
    static QStringList pathsFromResults(const QVariantMap &results);

private:
    void request(const QString &method, const QString &title, QVariantMap options, Callback callback);
};

// Connects the web view to the desktop: an export the page downloads is kept
// in a private folder until the user picks where it goes, and a file field on
// the page opens the portal's file chooser.
class Files : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    explicit Files(QQuickWebEngineProfile *profile, const QString &dataDirectory, QObject *parent = nullptr);

    QString notice() const { return m_notice; }

    // Called from QML's fileDialogRequested after accepting the request, so
    // the page waits for the portal's answer.
    Q_INVOKABLE void pickForPage(QObject *request, bool multiple);

    // Writes `source` to `target` atomically, so an existing file at the
    // target is either fully replaced or left as it was.
    static bool saveCopy(const QString &source, const QString &target, QString *error);

signals:
    void noticeChanged();

private:
    void downloadRequested(QQuickWebEngineDownloadRequest *download);
    void downloadFinished(QQuickWebEngineDownloadRequest *download, const QString &path);
    void tell(const QString &message);

    QString m_staging;
    QString m_lastFolder;
    QString m_notice;
    PortalFilePicker m_picker;
};
