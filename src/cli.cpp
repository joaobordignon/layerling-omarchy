// The command layout follows Hype's src/cli.cpp (MIT, David Heinemeier Hansson).
#include "cli.h"

#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace Cli {

static const char *usage =
    "Usage: layerling [command]\n"
    "\n"
    "  open [url]           open a window (the default), or one of layerling's\n"
    "                       own pages; a running layerling opens it instead\n"
    "  mcp                  run the MCP server for an AI client, over stdio;\n"
    "                       it opens an editor window in the app when needed\n"
    "  skill install        install the agent skill into ~/.claude/skills\n"
    "  --version            print the version\n"
    "  --help               print this help\n"
    "\n"
    "Register the MCP server with Claude Code once:\n"
    "  claude mcp add --scope user layerling -- layerling mcp\n";

// Loaded before the bridge script (node --import). Only a failed connection
// to the app's own address starts the app; timeouts and other errors, and any
// other address, pass through unchanged.
static const char *appLaunchModule = R"js(// Written by `layerling mcp`: starts the layerling app when a tool call finds it closed.
import { spawn } from "node:child_process";

const base = process.env.LAYERLING_URL;
const app = process.env.LAYERLING_APP;
const originalFetch = globalThis.fetch;
let starting = null;

async function answers() {
  try {
    await originalFetch(base + "/", { signal: AbortSignal.timeout(1500) });
    return true;
  } catch {
    return false;
  }
}

async function startApp() {
  spawn(app, ["open"], { stdio: "ignore", detached: true }).unref();
  for (let i = 0; i < 120; i++) {
    if (await answers()) return;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
}

globalThis.fetch = async (input, init = {}) => {
  try {
    return await originalFetch(input, init);
  } catch (error) {
    const url = String(input instanceof Request ? input.url : input);
    if (!base || !app || !url.startsWith(base) || error?.name === "TimeoutError" || error?.name === "AbortError") {
      throw error;
    }
    starting ??= startApp().finally(() => { starting = null; });
    await starting;
    // The call's own timeout ran while the app started: give the retry a new one.
    return originalFetch(input, { ...init, signal: AbortSignal.timeout(15000) });
  }
};
)js";

static int mcp() {
    const QString runtime = Paths::runtimeDirectory();
    const QString script = runtime + "/mcp/layerling-mcp-server.mjs";
    const QString node = Paths::nodeExecutable();
    if (node.isEmpty()) {
        fprintf(stderr, "layerling: Node.js was not found; install the nodejs package\n");
        return 1;
    }
    if (!QFileInfo::exists(script)) {
        fprintf(stderr, "layerling: %s is missing\n", qPrintable(script));
        return 1;
    }

    // When no editor is open, the bridge script opens one with xdg-open,
    // which would land in a web browser with none of the app's designs. A
    // shim first on its PATH hands that address to the app instead, which
    // starts the window if it is not running.
    const QString data = Paths::dataDirectory();
    const QString shimDirectory = data + "/bin";
    QDir().mkpath(shimDirectory);
    QSaveFile shim(shimDirectory + "/xdg-open");
    if (!shim.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "layerling: cannot write %s\n", qPrintable(shim.fileName()));
        return 1;
    }
    QString app = QCoreApplication::applicationFilePath();
    app.replace("'", "'\\''");
    shim.write(("#!/bin/sh\n# Written by `layerling mcp`: editors the MCP bridge opens go to the app.\nexec '" + app +
                "' open \"$@\"\n").toUtf8());
    if (!shim.commit()) {
        fprintf(stderr, "layerling: cannot write %s\n", qPrintable(shim.fileName()));
        return 1;
    }
    QFile::setPermissions(shim.fileName(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    // The bridge script gives up when the app's server does not answer. An
    // MCP client starts this command with every session, so opening the
    // window here would pop it up each time; instead a preload starts the app
    // the first time a tool call finds it closed, and retries that call.
    QSaveFile launcher(shimDirectory + "/app-launch.mjs");
    if (!launcher.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "layerling: cannot write %s\n", qPrintable(launcher.fileName()));
        return 1;
    }
    launcher.write(appLaunchModule);
    if (!launcher.commit()) {
        fprintf(stderr, "layerling: cannot write %s\n", qPrintable(launcher.fileName()));
        return 1;
    }

    qputenv("PATH", QFile::encodeName(shimDirectory) + ":" + qgetenv("PATH"));
    qputenv("LAYERLING_URL", Paths::baseUrl().toUtf8());
    qputenv("LAYERLING_MCP_TOKEN", Paths::mcpToken(data).toUtf8());
    qputenv("LAYERLING_APP", QFile::encodeName(QCoreApplication::applicationFilePath()));

    // Hand stdin and stdout straight to the script: the MCP client talks to it.
    const QByteArray nodePath = QFile::encodeName(node), scriptPath = QFile::encodeName(script);
    const QByteArray preload = QUrl::fromLocalFile(launcher.fileName()).toEncoded();
    char *const arguments[] = {const_cast<char *>(nodePath.constData()), const_cast<char *>("--import"),
                               const_cast<char *>(preload.constData()),
                               const_cast<char *>(scriptPath.constData()), nullptr};
    execv(nodePath.constData(), arguments);
    fprintf(stderr, "layerling: could not start %s: %s\n", nodePath.constData(), strerror(errno));
    return 1;
}

QString adaptSkill(const QString &skill) {
    const QString start = "## Quick Start", end = "## Targeting Editors";
    const qsizetype from = skill.indexOf(start), to = skill.indexOf(end);
    if (from < 0 || to < from)
        return skill;
    const QString quickStart =
        "## Quick Start\n"
        "\n"
        "layerling is installed as a desktop app. Its window runs a local server at\n" +
        Paths::baseUrl() +
        " with the MCP bridge switched on behind a token.\n"
        "\n"
        "1. The MCP server is `layerling mcp` (stdio). Register it once with\n"
        "   `claude mcp add --scope user layerling -- layerling mcp`.\n"
        "2. `layerling mcp` opens the layerling window itself when it is not running\n"
        "   and passes the address and token to the bridge; nothing else is needed.\n"
        "3. Ask the user to open or create a design in the window when\n"
        "   `layerling_list_editors` finds no editor.\n"
        "\n";
    return skill.left(from) + quickStart + skill.mid(to);
}

static bool copyTree(const QString &from, const QString &to, QString *error) {
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + "/" + QDir(from).relativeFilePath(source);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile in(source);
        QSaveFile out(target);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            *error = in.isOpen() ? out.errorString() : in.errorString();
            return false;
        }
        QByteArray contents = in.readAll();
        if (QFileInfo(source).fileName() == "SKILL.md")
            contents = adaptSkill(QString::fromUtf8(contents)).toUtf8();
        out.write(contents);
        if (!out.commit()) {
            *error = out.errorString();
            return false;
        }
    }
    return true;
}

static int skillInstall(bool json) {
    const QString source = Paths::runtimeDirectory() + "/skill";
    const QString target = QDir::homePath() + "/.claude/skills/layerling-mcp-skill";
    QString error;
    if (!QFileInfo::exists(source + "/SKILL.md"))
        error = source + "/SKILL.md is missing";
    else
        copyTree(source, target, &error);

    if (json) {
        QJsonObject result{{"ok", error.isEmpty()}, {"path", target}};
        if (!error.isEmpty())
            result.insert("error", error);
        printf("%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    }
    if (!error.isEmpty()) {
        fprintf(stderr, "layerling: %s\n", qPrintable(error));
        return 1;
    }
    if (!json)
        printf("Installed the skill in %s\n"
               "Register the MCP server once with:\n"
               "  claude mcp add --scope user layerling -- layerling mcp\n",
               qPrintable(target));
    return 0;
}

int run(int argc, char *argv[], QString *openUrl) {
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const bool json = arguments.removeAll("--json") > 0;

    if (arguments.isEmpty() || arguments == QStringList{"open"})
        return -1;
    if (arguments.size() == 2 && arguments.first() == "open") {
        *openUrl = arguments.at(1);
        return -1;
    }
    if (arguments == QStringList{"--help"} || arguments == QStringList{"-h"} || arguments == QStringList{"help"}) {
        printf("%s", usage);
        return 0;
    }

    // Everything below runs without a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(LAYERLING_APP_VERSION);

    if (arguments == QStringList{"--version"}) {
        printf("layerling %s\n", LAYERLING_APP_VERSION);
        return 0;
    }
    if (arguments == QStringList{"mcp"})
        return mcp();
    if (arguments == QStringList{"skill", "install"})
        return skillInstall(json);

    fprintf(stderr, "layerling: unknown command '%s'\n\n%s", qPrintable(arguments.join(' ')), usage);
    return 1;
}

}
