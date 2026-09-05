#include "artwork.h"
#include "backend.h"
#include "cli.h"
#include "settings.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QtGlobal>
#include <QIcon>
#include <QLibrary>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
// Fontconfig 2.15 still scans host templates when FONTCONFIG_FILE is set.
static bool initializeBundledFonts() {
    const auto path = qgetenv("PSYCHE_FONTCONFIG_FILE");
    if (path.isEmpty())
        return true;
    QLibrary library("fontconfig", 1);
    library.setLoadHints(QLibrary::PreventUnloadHint);
    using Create = void* (*)();
    using Parse = int (*)(void*, const unsigned char*, int);
    using Configure = int (*)(void*);
    using Destroy = void (*)(void*);
    const auto create = reinterpret_cast<Create>(library.resolve("FcConfigCreate"));
    const auto parse = reinterpret_cast<Parse>(library.resolve("FcConfigParseAndLoad"));
    const auto build = reinterpret_cast<Configure>(library.resolve("FcConfigBuildFonts"));
    const auto current = reinterpret_cast<Configure>(library.resolve("FcConfigSetCurrent"));
    const auto destroy = reinterpret_cast<Destroy>(library.resolve("FcConfigDestroy"));
    if (!create || !parse || !build || !current || !destroy)
        return false;
    void* config = create();
    if (!config)
        return false;
    const bool success =
        parse(config, reinterpret_cast<const unsigned char*>(path.constData()), 1) &&
        build(config) && current(config);
    destroy(config);
    return success;
}
int main(int argc, char** argv) {
    if (argc > 1) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("psyche");
        app.setApplicationVersion(PSYCHE_VERSION);
        return runCli();
    }
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");
    if (!initializeBundledFonts()) {
        qCritical("Unable to initialize bundled fonts.");
        return 1;
    }
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("psyche");
    app.setApplicationVersion(PSYCHE_VERSION);
    app.setDesktopFileName("psyche");
    app.setWindowIcon(QIcon(":/qt/qml/Psyche/qml/icons/psyche.svg"));
    AppSettings settings;
    Backend backend(&settings);
    Artwork artwork(settings.dataDirectory());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("preferences", &settings);
    engine.rootContext()->setContextProperty("artwork", &artwork);
    engine.loadFromModule("Psyche", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
