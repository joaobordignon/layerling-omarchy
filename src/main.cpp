// layerling — easy 3D CAD for 3D printing, as an Omarchy app: a window around
// layerling's own server, run locally so designs work offline and an AI
// client can drive the editor.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWebEngineProfile>
#include <QUrl>
#include <QtWebEngineQuick>

#include "backend.h"
#include "cli.h"
#include "files.h"
#include "instance.h"
#include "paths.h"
#include "theme.h"
#include "translator.h"

// The page reads navigator.languages, which follows Accept-Language, so the
// editor opens in the language picked for Omarchy where layerling has it.
static QString acceptLanguage(const QString &language) {
    const QString base = language.section('-', 0, 0);
    QStringList tags{language};
    if (base != language)
        tags << base;
    if (base != "en")
        tags << "en";
    return tags.join(',');
}

int main(int argc, char *argv[]) {
    QString openUrl;
    const int code = Cli::run(argc, argv, &openUrl);
    if (code >= 0)
        return code;

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    // One copy per user: a running one opens the window or page instead.
    if (Instance::forward(Instance::accepted(openUrl)))
        return 0;
    Instance instance(openUrl);
    if (!instance.listen())
        qWarning("layerling: cannot listen for other launches; a second launch will start its own window");
    app.setApplicationName("layerling");
    app.setApplicationVersion(LAYERLING_APP_VERSION);

    // Matches the window to layerling.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("layerling");
    app.setWindowIcon(QIcon::fromTheme("layerling"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;

    const QString data = Paths::dataDirectory();
    QDir().mkpath(data);

    // A named profile keeps designs (the page's own storage) between runs, in
    // the app's data folder rather than a browser's.
    QQuickWebEngineProfile profile("layerling");
    profile.setPersistentStoragePath(data + "/browser");
    profile.setCachePath(data + "/cache");
    profile.setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);
    profile.setHttpAcceptLanguage(acceptLanguage(translator.language()));
    QObject::connect(&translator, &Translator::changed, &profile,
                     [&] { profile.setHttpAcceptLanguage(acceptLanguage(translator.language())); });

    Backend backend(Paths::runtimeDirectory(), data, Paths::nodeExecutable());
    Files files(&profile, data);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &Backend::stop);

    // Lets QML compare backend.state with Backend.Ready.
    qmlRegisterUncreatableType<Backend>("Layerling", 1, 0, "Backend", "The app provides the backend.");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("files", &files);
    engine.rootContext()->setContextProperty("webProfile", &profile);
    engine.rootContext()->setContextProperty("instance", &instance);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    backend.start();
    return app.exec();
}
