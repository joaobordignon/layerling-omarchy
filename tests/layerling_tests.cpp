#include <QDBusArgument>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "cli.h"
#include "files.h"
#include "instance.h"
#include <QSignalSpy>
#include "paths.h"
#include "theme.h"
#include "translator.h"

class LayerlingTests : public QObject {
    Q_OBJECT

private slots:
    void serverTreeLinksRuntimeAndKeepsWritableFolders()
    {
        QTemporaryDir runtime, data;
        QDir(runtime.path()).mkpath("node_modules/next");
        QDir(runtime.path()).mkpath("apps/web/.next/server");
        QDir(runtime.path()).mkpath("apps/web/public");
        write(runtime.path() + "/package.json", "{}");
        write(runtime.path() + "/apps/web/server.js", "// server");
        write(runtime.path() + "/apps/web/.next/BUILD_ID", "x");

        QString error;
        const QString server = Paths::prepareServerTree(runtime.path(), data.path(), &error);
        QVERIFY2(!server.isEmpty(), qPrintable(error));
        const QString web = data.path() + "/server/apps/web";
        QCOMPARE(server, web + "/server.js");
        QVERIFY(QFileInfo(server).isSymLink());
        QVERIFY(QFileInfo(data.path() + "/server/node_modules").isSymLink());
        QVERIFY(QFileInfo(web + "/.next/BUILD_ID").isSymLink());
        // The folders the server writes into are real and the user's.
        QVERIFY(!QFileInfo(web + "/.next").isSymLink());
        QVERIFY(QFileInfo(web + "/.next/cache").isDir() && !QFileInfo(web + "/.next/cache").isSymLink());
        QVERIFY(QFileInfo(web + "/.codex/project-thumbnails").isDir());

        // A second start (an update moved things) rebuilds the links in place.
        write(runtime.path() + "/apps/web/.next/NEW", "y");
        QVERIFY(!Paths::prepareServerTree(runtime.path(), data.path(), &error).isEmpty());
        QVERIFY(QFileInfo(web + "/.next/NEW").isSymLink());
    }

    void serverTreeReportsMissingRuntime()
    {
        QTemporaryDir runtime, data;
        QString error;
        QVERIFY(Paths::prepareServerTree(runtime.path(), data.path(), &error).isEmpty());
        QCOMPARE(error, QString("missing"));
    }

    void tokenIsMadeOnceAndPrivate()
    {
        QTemporaryDir data;
        const QString token = Paths::mcpToken(data.path());
        QVERIFY(token.size() >= 16);
        QCOMPARE(Paths::mcpToken(data.path()), token);
        const auto permissions = QFileInfo(data.path() + "/mcp-token").permissions();
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::ReadOther)));
    }

    void portalResultsBecomePaths()
    {
        const QVariantMap results{{"uris", QStringList{"file:///home/me/part%20one.stl", "https://example.com/x"}}};
        QCOMPARE(PortalFilePicker::pathsFromResults(results), QStringList{"/home/me/part one.stl"});
        QVERIFY(PortalFilePicker::pathsFromResults({}).isEmpty());
    }

    void saveCopyReplacesTarget()
    {
        QTemporaryDir dir;
        write(dir.path() + "/export.stl", "solid new");
        write(dir.path() + "/kept.stl", "solid old");
        QString error;
        QVERIFY(Files::saveCopy(dir.path() + "/export.stl", dir.path() + "/kept.stl", &error));
        QFile kept(dir.path() + "/kept.stl");
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("solid new"));
        QVERIFY(!Files::saveCopy(dir.path() + "/absent.stl", dir.path() + "/x.stl", &error));
        QVERIFY(!QFileInfo::exists(dir.path() + "/x.stl"));
    }

    void skillGetsTheAppsQuickStart()
    {
        const QString skill = "---\nname: s\n---\n# t\n\n## Quick Start\n\nnpm run dev\n\n## Client Compatibility\n\nx\n\n## Targeting Editors\n\nkeep\n";
        const QString adapted = Cli::adaptSkill(skill);
        QVERIFY(!adapted.contains("npm run dev"));
        QVERIFY(adapted.contains("layerling mcp"));
        QVERIFY(adapted.contains("## Targeting Editors\n\nkeep"));
        QVERIFY(adapted.startsWith("---\nname: s"));
        QCOMPARE(Cli::adaptSkill("no sections"), QString("no sections"));
    }

    void instanceHandsPagesToTheRunningCopy()
    {
        QTemporaryDir dir;
        const QString socket = dir.path() + "/layerling.sock";
        QVERIFY(!Instance::forward("", socket));

        Instance running("");
        QVERIFY(running.listen(socket));
        QSignalSpy opened(&running, &Instance::openRequested);
        const QString editor = Paths::baseUrl() + "/?editor=1";
        QVERIFY(Instance::forward(editor, socket));
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), editor);

        // Anything but the app's own pages only brings up a window.
        QVERIFY(Instance::forward("https://example.com/", socket));
        QTRY_COMPARE(opened.count(), 2);
        QCOMPARE(opened.at(1).at(0).toString(), QString());
    }

    void instanceAcceptsOnlyOwnPages()
    {
        QCOMPARE(Instance::accepted(Paths::baseUrl() + "/?editor=1"), Paths::baseUrl() + "/?editor=1");
        QVERIFY(Instance::accepted("http://127.0.0.1:1/").isEmpty());
        QVERIFY(Instance::accepted(Paths::baseUrl() + ".evil.com/").isEmpty());
        QVERIFY(Instance::accepted("").isEmpty());
        QCOMPARE(Instance("file:///etc/passwd").initialUrl(), QString());
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(LayerlingTests)
#include "layerling_tests.moc"
