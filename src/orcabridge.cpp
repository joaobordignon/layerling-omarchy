#include "orcabridge.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <cstring>
#include <iterator>

namespace {

constexpr int kTimeoutMs = 5 * 60 * 1000;
constexpr qsizetype kMaxModelBytes = 300 * 1024 * 1024;
constexpr int kProjectsKept = 20;
const char *const kKinds[] = {"machine", "process", "filament"};

QString orcaProgram() {
    const QString found = QStandardPaths::findExecutable("orca-slicer");
    return found.isEmpty() ? QStringLiteral("/usr/bin/orca-slicer") : found;
}

QJsonObject readJsonFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// A design name as a file name: no folders, nothing the shell or Orca's
// ';'-separated preset lists could trip over.
QString safeStem(const QString &fileName) {
    QString stem = QFileInfo(fileName).completeBaseName();
    stem.replace(QRegularExpression("[^A-Za-z0-9 _.-]+"), "_");
    stem = stem.trimmed().left(60);
    return stem.isEmpty() ? QStringLiteral("layerling design") : stem;
}

void pruneProjects(const QString &directory) {
    QDir dir(directory);
    const QFileInfoList projects = dir.entryInfoList({"*.3mf"}, QDir::Files, QDir::Time);
    for (int index = kProjectsKept; index < projects.size(); ++index)
        QFile::remove(projects.at(index).absoluteFilePath());
}

}  // namespace

OrcaBridge::OrcaBridge(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_work(dataDirectory + "/orca") {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::finished, this, &OrcaBridge::jobFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_job.id) {
            const int id = m_job.id;
            m_job = {};
            fail(id, tr("OrcaSlicer could not be started (%1). Is it installed?").arg(orcaProgram()));
        }
    });
}

QString OrcaBridge::orcaDataDirectory() const {
    return QDir::homePath() + "/.config/OrcaSlicer";
}

// Orca keeps user presets per account under user/<id>; the folder written
// most recently is the one in use.
QString OrcaBridge::userPresetDirectory() const {
    QDir users(orcaDataDirectory() + "/user");
    QString best;
    QDateTime newest;
    for (const QFileInfo &entry : users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo machines(entry.absoluteFilePath() + "/machine");
        if (!machines.isDir())
            continue;
        QDateTime latest = machines.lastModified();
        for (const char *kind : kKinds) {
            const QFileInfo folder(entry.absoluteFilePath() + "/" + kind);
            if (folder.isDir() && folder.lastModified() > latest)
                latest = folder.lastModified();
        }
        if (best.isEmpty() || latest > newest) {
            best = entry.absoluteFilePath();
            newest = latest;
        }
    }
    return best;
}

// The file of the user preset with this name, or "" when there is none. Only
// user presets: Orca's command line resolves their `inherits` chain itself,
// while a built-in preset has no file it can load.
QString OrcaBridge::presetFile(const QString &kind, const QString &name) const {
    const QString root = userPresetDirectory();
    if (root.isEmpty() || name.isEmpty())
        return {};
    QDir dir(root + "/" + kind);
    for (const QFileInfo &file : dir.entryInfoList({"*.json"}, QDir::Files)) {
        const QString presetName = readJsonFile(file.absoluteFilePath()).value("name").toString(file.completeBaseName());
        if (presetName == name)
            return file.absoluteFilePath();
    }
    return {};
}

QJsonObject OrcaBridge::listPresets() const {
    QJsonObject result;
    const QString root = userPresetDirectory();
    result["ok"] = true;
    result["orcaFound"] = QFileInfo(orcaProgram()).isExecutable();
    result["slicer"] = "OrcaSlicer";
    const char *const keys[] = {"machines", "processes", "filaments"};
    for (int index = 0; index < 3; ++index) {
        QStringList names;
        if (!root.isEmpty()) {
            QDir dir(root + "/" + kKinds[index]);
            for (const QFileInfo &file : dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name))
                names << readJsonFile(file.absoluteFilePath()).value("name").toString(file.completeBaseName());
        }
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        result[keys[index]] = QJsonArray::fromStringList(names);
    }

    // What Orca had selected last, as a starting point for the pickers.
    QJsonObject current;
    const QJsonObject config = readJsonFile(orcaDataDirectory() + "/OrcaSlicer.conf");
    const QString machine = config.value("presets").toObject().value("machine").toString();
    current["machine"] = machine;
    for (const QJsonValue &entry : config.value("orca_presets").toArray()) {
        const QJsonObject preset = entry.toObject();
        if (preset.value("machine").toString() == machine) {
            current["process"] = preset.value("process").toString();
            current["filament"] = preset.value("filament").toString().section(';', 0, 0);
            break;
        }
    }
    result["current"] = current;
    return result;
}

void OrcaBridge::presets(int id) {
    reply(id, listPresets());
}

void OrcaBridge::slice(int id, const QString &request) {
    run(id, request, false);
}

void OrcaBridge::open(int id, const QString &request) {
    run(id, request, true);
}

void OrcaBridge::run(int id, const QString &request, bool openAfter) {
    if (m_job.id) {
        fail(id, tr("OrcaSlicer is still busy with the last request."));
        return;
    }
    const QJsonObject body = QJsonDocument::fromJson(request.toUtf8()).object();
    const QString machine = presetFile("machine", body.value("machine").toString());
    const QString process = presetFile("process", body.value("process").toString());
    const QString filament = presetFile("filament", body.value("filament").toString());
    if (machine.isEmpty() || process.isEmpty() || filament.isEmpty()) {
        fail(id, tr("Pick a printer, a process and a filament from your OrcaSlicer presets."));
        return;
    }
    const QByteArray model = QByteArray::fromBase64(body.value("base64").toString().toLatin1());
    if (model.isEmpty() || model.size() > kMaxModelBytes) {
        fail(id, tr("The design could not be prepared for OrcaSlicer."));
        return;
    }

    // One working folder per request, replaced by the next.
    const QString directory = m_work + "/job";
    QDir(directory).removeRecursively();
    if (!QDir().mkpath(directory)) {
        fail(id, tr("The folder %1 could not be created.").arg(directory));
        return;
    }
    const QString stem = safeStem(body.value("fileName").toString());
    const QString modelPath = directory + "/" + stem + ".stl";
    QFile file(modelPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(model) != model.size()) {
        fail(id, tr("The design could not be written to %1.").arg(modelPath));
        return;
    }
    file.close();

    QStringList arguments{"--datadir", orcaDataDirectory(),
                          "--arrange", "1",
                          "--load-settings", machine + ";" + process,
                          "--load-filaments", filament,
                          "--outputdir", directory};
    m_job = {id, openAfter, directory, {}};
    if (openAfter) {
        // A project: the model on the plate with these presets, kept so the
        // Orca window that opens it can save it again.
        const QString projects = m_work + "/projects";
        QDir().mkpath(projects);
        m_job.project = projects + "/" + stem + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmmss") + ".3mf";
        // Orca takes a bare file name here and writes it into --outputdir; a
        // path makes the export fail. It is moved to the projects folder after.
        arguments << "--export-3mf" << "project.3mf";
    } else {
        arguments << "--slice" << "0";
    }
    arguments << modelPath;

    m_process.setWorkingDirectory(directory);
    m_process.start(orcaProgram(), arguments);
    QTimer::singleShot(kTimeoutMs, this, [this, id] {
        if (m_job.id == id && m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
}

void OrcaBridge::jobFinished(int exitCode, QProcess::ExitStatus status) {
    const Job job = m_job;
    m_job = {};
    if (!job.id)
        return;
    const QString output = QString::fromUtf8(m_process.readAll()).trimmed();
    const QJsonObject result = readJsonFile(job.directory + "/result.json");
    if (status != QProcess::NormalExit || exitCode != 0 || result.value("return_code").toInt(-1) != 0) {
        QString error = result.value("error_string").toString();
        if (error.isEmpty() || error == "Success.")
            error = output.section('\n', -1).left(300);
        fail(job.id, tr("OrcaSlicer could not %1 the design: %2")
                         .arg(job.openAfter ? tr("prepare") : tr("slice"),
                              error.isEmpty() ? tr("it stopped (exit %1)").arg(exitCode) : error));
        return;
    }

    if (job.openAfter) {
        const QString written = job.directory + "/project.3mf";
        QFile::remove(job.project);
        if (!QFileInfo(written).isFile() || !QFile::rename(written, job.project)) {
            fail(job.id, tr("OrcaSlicer did not write the project."));
            return;
        }
        // A new Orca window with the project; the one already open stays as it is.
        if (!QProcess::startDetached(orcaProgram(), {job.project})) {
            fail(job.id, tr("OrcaSlicer could not be started."));
            return;
        }
        pruneProjects(QFileInfo(job.project).absolutePath());
        QJsonObject done{{"ok", true}, {"project", job.project}};
        reply(job.id, done);
        return;
    }
    reply(job.id, readSliceResult(job.directory));
}

// Print time, filament and cost from the G-code Orca wrote, plus its warnings.
QJsonObject OrcaBridge::readSliceResult(const QString &directory) const {
    QJsonObject result{{"ok", true}};
    const QJsonObject summary = readJsonFile(directory + "/result.json");
    QJsonArray warnings;
    for (const QJsonValue &warning : summary.value("warnings").toArray()) {
        const QString text = warning.isObject() ? warning.toObject().value("msg").toString() : warning.toString();
        if (!text.isEmpty())
            warnings << text;
    }
    for (const QJsonValue &plate : summary.value("sliced_plates").toArray()) {
        const QString text = plate.toObject().value("warning_message").toString();
        if (!text.isEmpty())
            warnings << text;
    }
    result["warnings"] = warnings;

    const QFileInfoList gcodes = QDir(directory).entryInfoList({"*.gcode"}, QDir::Files, QDir::Name);
    if (gcodes.isEmpty())
        return QJsonObject{{"ok", false}, {"error", tr("OrcaSlicer wrote no G-code.")}};
    QFile gcode(gcodes.first().absoluteFilePath());
    if (!gcode.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject{{"ok", false}, {"error", tr("The G-code could not be read.")}};
    const struct { const char *prefix; const char *key; bool number; } fields[] = {
        {"; estimated printing time (normal mode) = ", "time", false},
        {"; total filament used [g] = ", "grams", true},
        {"; total filament cost = ", "cost", true},
        {"; total layers count = ", "layers", true},
        {"; filament used [mm] = ", "millimetres", true},
    };
    int found = 0;
    QTextStream stream(&gcode);
    QString line;
    while (found < int(std::size(fields)) && stream.readLineInto(&line)) {
        if (!line.startsWith(';'))
            continue;
        for (const auto &field : fields) {
            if (result.contains(field.key) || !line.startsWith(field.prefix))
                continue;
            const QString value = line.mid(int(strlen(field.prefix))).trimmed();
            if (field.number)
                result[field.key] = value.section(',', 0, 0).toDouble();
            else
                result[field.key] = value;
            ++found;
        }
    }
    result["layerHeight"] = summary.value("layer_height").toDouble();
    result["infill"] = summary.value("sparse_infill_density").toDouble();
    result["walls"] = summary.value("wall_loops").toInt();
    return result;
}

void OrcaBridge::reply(int id, const QJsonObject &result) {
    emit finished(id, QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

void OrcaBridge::fail(int id, const QString &error) {
    reply(id, QJsonObject{{"ok", false}, {"error", error}});
}
