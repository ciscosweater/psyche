#include "artwork.h"
#include "backend.h"
#include "test_helpers.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
static void waitFrame() {
    QEventLoop loop;
    QTimer::singleShot(120, &loop, &QEventLoop::quit);
    loop.exec();
}
int main(int argc, char** argv) {
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("psyche");
    app.setApplicationVersion(PSYCHE_VERSION);
    QTemporaryDir temp;
    if (argc < 2 || !temp.isValid())
        return 1;
    qputenv("PSYCHE_HUBCAP_API_KEY", "");
    auto home = temp.path() + "/home";
    QDir().mkpath(home + "/.config/SLSsteam");
    QFile f(home + "/.config/SLSsteam/config.yaml");
    if (!f.open(QIODevice::WriteOnly))
        return 1;
    f.write("{}");
    f.close();
    AppSettings settings(
        home, home + "/.local/share/psyche", home + "/.config", home + "/.local/share");
    Backend backend(&settings, nullptr, QUrl("http://127.0.0.1:1/api"));
    QDir().mkpath(settings.dataDirectory() + "/cache/covers");
    QImage fixtureCover(460, 215, QImage::Format_RGB32);
    fixtureCover.fill(QColor("#326da8"));
    if (!fixtureCover.save(settings.dataDirectory() + "/cache/covers/123.jpg"))
        return 1;
    Artwork artwork(settings.dataDirectory(),
                    nullptr,
                    QUrl("http://127.0.0.1:1/api"),
                    QUrl("http://127.0.0.1:1/cdn/"));
    QQmlApplicationEngine engine;
    bool warnings = false;
    QObject::connect(
        &engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
            warnings = true;
            for (const auto& error : errors)
                std::cerr << error.toString().toStdString() << '\n';
        });
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("preferences", &settings);
    engine.rootContext()->setContextProperty("artwork", &artwork);
    engine.load(QUrl::fromLocalFile(argv[1]));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto tabs = window->findChild<QObject*>("mainTabs");
    if (!tabs)
        return 1;
    waitFrame();
    auto font = window->findChild<QObject*>("pixelFont");
    if (!font || font->property("status").toInt() != 1)
        return 1;
    for (const auto& theme : {"dark"}) {
        settings.savePreferences("", false, theme);
        for (int width : {1000, 620}) {
            window->resize(width, width == 620 ? 580 : 800);
            for (int tab = 0; tab < 5; ++tab) {
                tabs->setProperty("currentIndex", tab);
                waitFrame();
                if (tab == 1) {
                    auto card = window->findChild<QObject*>("zipCard");
                    if (!card || card->property("height").toDouble() < 140)
                        return 1;
                }
                auto screenshot = window->grabWindow();
                if (screenshot.isNull())
                    return 1;
                if (argc > 2) {
                    QDir().mkpath(argv[2]);
                    screenshot.save(
                        QString("%1/%2-%3-tab%4.png").arg(argv[2], theme).arg(width).arg(tab));
                }
            }
        }
    }
    // Import, apply, and restore against a local ZIP fixture.
    zip(temp.path() + "/fixture.zip", "game.lua", "addappid(123)");
    auto finishWork = [&] {
        QElapsedTimer timer;
        timer.start();
        while (backend.busy() && timer.elapsed() < 3000)
            waitFrame();
        return !backend.busy();
    };
    backend.inspect(QUrl::fromLocalFile(temp.path() + "/fixture.zip"));
    backend.openFolder(temp.path() + "/missing-backup");
    if (!backend.busy() || backend.activity() != "import")
        return 1;
    if (!finishWork() || !backend.ready() || tabs->property("currentIndex").toInt() != 1)
        return 1;
    waitFrame();
    auto readyCard = window->findChild<QObject*>("readyCard");
    if (!readyCard || readyCard->property("height").toDouble() <= 0)
        return 1;
    window->resize(900, 700);
    waitFrame();
    if (readyCard->property("height").toDouble() <= 0)
        return 1;
    window->resize(1000, 800);
    waitFrame();
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/import-ready.png");
    auto entriesToggle = window->findChild<QObject*>("entriesToggle");
    if (!entriesToggle)
        return 1;
    entriesToggle->setProperty("checked", true);
    waitFrame();
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/import-details.png");
    backend.apply();
    if (!finishWork() || backend.statusKind() != "success" || !backend.applied())
        return 1;
    waitFrame();
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/import-applied.png");
    tabs->setProperty("currentIndex", 3);
    waitFrame();
    auto history = window->findChild<QObject*>("historyList");
    if (!history || history->property("count").toInt() != 1)
        return 1;
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/history.png");
    tabs->setProperty("currentIndex", 2);
    waitFrame();
    if (backend.installed().size() != 1)
        return 1;
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/library.png");
    backend.removeGame(backend.installed().first().toMap()["id"].toString());
    if (!finishWork() || backend.statusKind() != "success" || !backend.installed().isEmpty()) {
        std::cerr << backend.status().toStdString() << "\n";
        return 1;
    }
    backend.restore(0);
    if (!finishWork() || backend.installed().size() != 1)
        return 1;
    backend.restore(1);
    if (!finishWork() || backend.statusKind() != "success" || backend.applied())
        return 1;
    AppSettings reloaded(
        home, home + "/.local/share/psyche", home + "/.config", home + "/.local/share");
    if (reloaded.history().size() != 2 || !reloaded.history().last().toMap()["restored"].toBool())
        return 1;
    // Render a search-result row without hitting the network.
    tabs->setProperty("currentIndex", 0);
    auto results = window->findChild<QObject*>("searchResults");
    results->setProperty(
        "model",
        QVariantList{QVariantMap{
            {"appId", "123"},
            {"name", "Fixture Game with a long name to check compact layout and keyboard focus"}}});
    waitFrame();
    if (argc > 2)
        window->grabWindow().save(QString(argv[2]) + "/search-result.png");
    if (argc > 3 && QString(argv[3]) == "--live-artwork") {
        Artwork liveArtwork(settings.dataDirectory());
        engine.rootContext()->setContextProperty("artwork", &liveArtwork);
        settings.savePreferences("", false, "dark");
        window->resize(1000, 800);
        results->setProperty(
            "model",
            QVariantList{QVariantMap{{"appId", "620"}, {"name", "Portal 2"}},
                         QVariantMap{{"appId", "730"}, {"name", "Counter-Strike 2"}}});
        QElapsedTimer timer;
        timer.start();
        while ((liveArtwork.states()["620"] != "ready" || liveArtwork.states()["730"] != "ready") &&
               timer.elapsed() < 25000)
            waitFrame();
        if (liveArtwork.states()["620"] != "ready" || liveArtwork.states()["730"] != "ready")
            return 1;
        waitFrame();
        window->grabWindow().save(QString(argv[2]) + "/steam-artwork.png");
        results->setProperty("model", QVariantList{});
        engine.rootContext()->setContextProperty("artwork", &artwork);
    }
    if (warnings)
        return 1;
    std::cout << "QML loaded and rendered all tabs at desktop/compact sizes with the bundled pixel "
                 "font and dark theme.\n";
    return 0;
}
