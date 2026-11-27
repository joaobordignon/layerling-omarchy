#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

// The page's way to OrcaSlicer, over the web channel as `orca` (see
// orca-desktop.js, which wraps it as window.layerlingDesktop):
//   presets(id)           Orca's user presets: printers, processes, filaments
//   slice(id, request)    slices the model with the chosen presets in the
//                         background and answers with time, filament and cost
//   open(id, request)     builds an Orca project (model on the plate, the
//                         chosen presets) and opens it in a new Orca window
// Each call answers once with finished(id, json). A request is JSON:
//   { base64, fileName, machine, process, filament }
// The work happens in Orca's own command line (orca-slicer --datadir ...),
// which reads Orca's settings and never writes them.
class OrcaBridge : public QObject {
    Q_OBJECT

public:
    explicit OrcaBridge(const QString &dataDirectory, QObject *parent = nullptr);

    Q_INVOKABLE void presets(int id);
    Q_INVOKABLE void slice(int id, const QString &request);
    Q_INVOKABLE void open(int id, const QString &request);

signals:
    void finished(int id, const QString &result);

private:
    struct Job {
        int id = 0;
        bool openAfter = false;
        QString directory;
        QString project;
    };

    QString orcaDataDirectory() const;
    QString userPresetDirectory() const;
    QString presetFile(const QString &kind, const QString &name) const;
    QJsonObject listPresets() const;
    void run(int id, const QString &request, bool openAfter);
    void jobFinished(int exitCode, QProcess::ExitStatus status);
    QJsonObject readSliceResult(const QString &directory) const;
    void reply(int id, const QJsonObject &result);
    void fail(int id, const QString &error);

    QString m_work;
    QProcess m_process;
    Job m_job;
};
