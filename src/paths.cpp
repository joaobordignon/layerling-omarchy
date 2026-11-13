#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace Paths {

QString baseUrl() { return QStringLiteral("http://127.0.0.1:%1").arg(port); }

QString runtimeDirectory() {
    const QString fromEnvironment = qEnvironmentVariable("LAYERLING_RUNTIME");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    const QString beside = QCoreApplication::applicationDirPath() + "/runtime";
    if (QFileInfo::exists(beside + "/apps/web/server.js"))
        return beside;
    return QStringLiteral("/usr/lib/layerling");
}

QString dataDirectory() {
    const QString fromEnvironment = qEnvironmentVariable("LAYERLING_DATA");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    return QDir::homePath() + "/.local/share/layerling";
}

QString nodeExecutable() {
    const QString fromEnvironment = qEnvironmentVariable("LAYERLING_NODE");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    // The system package first: it is what the package depends on, and a
    // version manager's node may change under the app.
    if (QFileInfo(QStringLiteral("/usr/bin/node")).isExecutable())
        return QStringLiteral("/usr/bin/node");
    return QStandardPaths::findExecutable("node");
}

QString mcpToken(const QString &dataDirectory) {
    const QString path = dataDirectory + "/mcp-token";
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        const QString token = QString::fromLatin1(existing.readAll()).trimmed();
        if (token.size() >= 16)
            return token;
    }

    QString token;
    for (int i = 0; i < 4; ++i)
        token += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');

    QDir().mkpath(dataDirectory);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(token.toLatin1() + '\n');
        file.commit();
    }
    return token;
}

// Replaces whatever is at `link` with a symbolic link to `target`.
static bool linkTo(const QString &target, const QString &link) {
    QFileInfo info(link);
    if (info.isSymLink() && info.symLinkTarget() == target)
        return true;
    if (info.isSymLink() || info.isFile())
        QFile::remove(link);
    else if (info.isDir())
        QDir(link).removeRecursively();
    return QFile::link(target, link);
}

// Mirrors `source` into `destination` with one link per entry, except the
// entries named in `writable`, which become real folders of their own.
static bool mirror(const QString &source, const QString &destination, const QStringList &writable,
                   QString *error) {
    if (!QDir().mkpath(destination)) {
        *error = QStringLiteral("cannot create %1").arg(destination);
        return false;
    }
    const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                    QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (writable.contains(name))
            continue;
        if (!linkTo(entry.absoluteFilePath(), destination + "/" + name)) {
            *error = QStringLiteral("cannot link %1").arg(destination + "/" + name);
            return false;
        }
    }
    for (const QString &name : writable) {
        const QString path = destination + "/" + name;
        if (QFileInfo(path).isSymLink())
            QFile::remove(path);
        if (!QDir().mkpath(path)) {
            *error = QStringLiteral("cannot create %1").arg(path);
            return false;
        }
    }
    return true;
}

QString prepareServerTree(const QString &runtime, const QString &dataDirectory, QString *error) {
    const QString web = runtime + "/apps/web";
    if (!QFileInfo::exists(web + "/server.js")) {
        *error = QStringLiteral("missing");
        return {};
    }
    const QString tree = dataDirectory + "/server";
    // The top level holds node_modules and package.json; apps/web holds the
    // server, with .next/cache and .codex as the folders it writes into.
    if (!mirror(runtime, tree, {"apps"}, error) || !mirror(runtime + "/apps", tree + "/apps", {"web"}, error) ||
        !mirror(web, tree + "/apps/web", {".next", ".codex"}, error) ||
        !mirror(web + "/.next", tree + "/apps/web/.next", {"cache"}, error))
        return {};
    QDir().mkpath(tree + "/apps/web/.codex/project-thumbnails");
    return tree + "/apps/web/server.js";
}

}
