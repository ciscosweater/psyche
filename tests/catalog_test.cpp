#include "catalog.h"
#include "test_helpers.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <memory>
#include <yaml-cpp/yaml.h>
struct Response {
    int status = 200;
    QByteArray body;
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    try {
        PSYCHE_CHECK(tmp.isValid());
        QTcpServer server;
        PSYCHE_CHECK(server.listen(QHostAddress::LocalHost));
        QList<Response> responses;
        QList<QByteArray> requests;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                *request += socket->readAll();
                if (!request->contains("\r\n\r\n"))
                    return;
                requests.append(*request);
                Response response = responses.isEmpty() ? Response{500, "unexpected request"}
                                                        : responses.takeFirst();
                socket->write("HTTP/1.1 " + QByteArray::number(response.status) +
                              " Response\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(response.body.size()) + "\r\n\r\n" +
                              response.body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        const auto hubcap = QUrl(QString("http://127.0.0.1:%1/api/v1").arg(server.serverPort()));
        const auto steamcmd = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        auto steamcmdApp = [](const QString& id) {
            return QJsonDocument(
                       QJsonObject{{"status", "success"},
                                   {"data",
                                    QJsonObject{{id,
                                                 QJsonObject{{"appid", id},
                                                             {"common",
                                                              QJsonObject{{"name", "Steam app"},
                                                                          {"type", "Game"}}}}}}}})
                .toJson(QJsonDocument::Compact);
        };
        auto steamcmdDepot = [](const QString& id) {
            return QJsonDocument(QJsonObject{{"status", "success"},
                                             {"data", QJsonObject{{id, QJsonObject()}}}})
                .toJson(QJsonDocument::Compact);
        };
        Catalog catalog("test-key", hubcap, steamcmd);
        responses.append(
            {200, R"({"games":[{"game_id":123,"game_name":"Fixture Game"}],"total_count":101})"});
        auto page = catalog.search("fiction & game");
        PSYCHE_CHECK(page.games.size() == 1 && page.hasMore);
        PSYCHE_CHECK(page.games.first().toMap()["appId"] == "123");
        PSYCHE_CHECK(requests.last().toLower().contains("authorization: bearer test-key"));
        PSYCHE_CHECK(requests.last().contains("search=fiction%20%26%20game"));
        responses.append(
            {200, R"({"results":[{"appid":"456","name":"Second Game"}],"total_count":101})"});
        page = catalog.search("fiction", 100);
        PSYCHE_CHECK(!page.hasMore);
        PSYCHE_CHECK(requests.last().contains("offset=100"));
        responses.append({200, "[]"});
        PSYCHE_CHECK(catalog.search("empty").games.isEmpty());
        zip(tmp.path() + "/package.zip", "game.lua", "addappid(123)");
        QFile f(tmp.path() + "/package.zip");
        PSYCHE_CHECK(f.open(QIODevice::ReadOnly));
        responses.append({200, f.readAll()});
        auto p = catalog.fetch("123", "zip");
        PSYCHE_CHECK(p.apps.contains("123"));
        PSYCHE_CHECK(requests.last().startsWith("GET /api/v1/manifest/123 "));
        auto rejects = [&](auto action) {
            bool rejected = false;
            try {
                action();
            } catch (const std::exception&) {
                rejected = true;
            }
            PSYCHE_CHECK(rejected);
        };
        responses.append({200, R"({"status":"healthy"})"});
        PSYCHE_CHECK(catalog.health()["status"] == "healthy");
        PSYCHE_CHECK(requests.last().startsWith("GET /api/v1/health "));
        PSYCHE_CHECK(!requests.last().toLower().contains("authorization:"));
        responses.append({503, R"({"status":"degraded"})"});
        PSYCHE_CHECK(catalog.health()["status"] == "degraded");
        responses.append(
            {200,
             R"({"username":"fixture","daily_usage":0,"daily_limit":0,"can_make_requests":false,"api_key":"must-not-be-returned"})"});
        auto stats = catalog.stats();
        PSYCHE_CHECK(stats["daily_usage"].toInt() == 0 && stats["daily_limit"].toInt() == 0);
        PSYCHE_CHECK(!stats.contains("api_key"));
        PSYCHE_CHECK(requests.last().startsWith("GET /api/v1/user/stats "));
        PSYCHE_CHECK(requests.last().toLower().contains("authorization: bearer test-key"));
        responses.append({200, "{}"});
        rejects([&] { catalog.stats(); });
        responses.append({200, "invalid"});
        rejects([&] { catalog.health(); });
        responses.append({401, "unauthorized"});
        try {
            catalog.stats();
            PSYCHE_CHECK(false);
        } catch (const std::exception& e) {
            auto message = QString::fromUtf8(e.what());
            PSYCHE_CHECK(message.contains("401") && message.contains("expired"));
        }
        responses.append({200, R"({"status":"healthy"})"});
        PSYCHE_CHECK(
            Catalog("", QUrl(QString("http://127.0.0.1:%1/api/v1").arg(server.serverPort())))
                .health()["status"] == "healthy");
        const QStringList modes{"full", "basegame", "dlc"};
        const QStringList endpoints{"/lua/123", "/lua/basegame/123", "/lua/dlc/123"};
        for (int i = 0; i < modes.size(); ++i) {
            auto before = requests.size();
            responses.append(
                {200, "addappid(123)\nsetdepotkey(456, \"" + QByteArray(64, 'a') + "\")"});
            responses.append({200, steamcmdDepot("456")});
            auto lua = catalog.fetch("123", modes[i]);
            PSYCHE_CHECK(lua.apps.contains("123") && lua.keys.contains("456"));
            PSYCHE_CHECK(!lua.apps.contains("456"));
            PSYCHE_CHECK(requests.size() == before + 2);
            PSYCHE_CHECK(requests[before].startsWith("GET /api/v1" + endpoints[i].toUtf8() + " "));
            PSYCHE_CHECK(requests.last().contains("/v1/info/456"));
        }
        responses.append({200, "addappid(123)"});
        PSYCHE_CHECK(catalog.fetch("123").apps.contains("123"));
        PSYCHE_CHECK(requests.last().startsWith("GET /api/v1/lua/123 "));
        // Depot keys only: do not invent AdditionalApps when Steam says the ID is a depot.
        for (const auto& mode : QStringList{"full", "basegame", "dlc", "zip"}) {
            QByteArray body = "addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")";
            if (mode == "zip") {
                zip(tmp.path() + "/depots.zip", "game.lua", body);
                QFile archive(tmp.path() + "/depots.zip");
                PSYCHE_CHECK(archive.open(QIODevice::ReadOnly));
                body = archive.readAll();
            }
            auto before = requests.size();
            responses.append({200, body});
            responses.append({200, steamcmdDepot("2062430")});
            auto imported = catalog.fetch("2062430", mode);
            PSYCHE_CHECK(!imported.apps.contains("2062430") && imported.depots.contains("2062430"));
            PSYCHE_CHECK(imported.mainAppId == "2062430");
            PSYCHE_CHECK(!imported.games.first().toObject()["apps"].toArray().contains("2062430"));
            PSYCHE_CHECK(requests.size() == before + 2);
            PSYCHE_CHECK(requests.last().contains("/v1/info/2062430"));
        }
        // Main AppID with a decryption key: Steam says it is an app, so promote it.
        {
            QByteArray body = "addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")";
            responses.append({200, body});
            responses.append({200, steamcmdApp("2062430")});
            auto promoted = catalog.fetch("2062430");
            PSYCHE_CHECK(promoted.apps.contains("2062430") && promoted.depots.contains("2062430"));
            PSYCHE_CHECK(promoted.games.first().toObject()["apps"].toArray().contains("2062430"));
        }
        // steamcmd failure must not invent AdditionalApps and must not fail the import.
        {
            QByteArray body = "addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")";
            responses.append({200, body});
            responses.append({500, "error"});
            auto closed = catalog.fetch("2062430");
            PSYCHE_CHECK(!closed.apps.contains("2062430") && closed.depots.contains("2062430"));
        }
        {
            auto keyed = readLuaPackage("addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")");
            auto before = requests.size();
            Catalog::promoteKeyedApps(keyed, QUrl());
            PSYCHE_CHECK(!keyed.apps.contains("2062430") && keyed.depots.contains("2062430"));
            PSYCHE_CHECK(requests.size() == before);
            responses.append({200, steamcmdApp("2062430")});
            Catalog::promoteKeyedApps(keyed, steamcmd);
            PSYCHE_CHECK(keyed.apps.contains("2062430"));
            PSYCHE_CHECK(requests.last().contains("/v1/info/2062430"));
            PSYCHE_CHECK(!requests.last().toLower().contains("authorization:"));
        }
        {
            const auto key = QByteArray(64, 'a');
            auto mixed = readLuaPackage("addappid(2062430, 1, \"" + key +
                                        "\")\nsetdepotkey(2062431, \"" + key + "\")");
            PSYCHE_CHECK(mixed.apps.isEmpty() &&
                         mixed.depots == QSet<QString>{"2062430", "2062431"});
            responses.append({200, steamcmdApp("2062430")});
            responses.append({200, steamcmdDepot("2062431")});
            Catalog::promoteKeyedApps(mixed, steamcmd);
            PSYCHE_CHECK(mixed.apps == QSet<QString>{"2062430"});
            PSYCHE_CHECK(mixed.depots == QSet<QString>{"2062430", "2062431"});
        }
        {
            auto keyed = readLuaPackage("addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")");
            responses.append(
                {200,
                 QJsonDocument(
                     QJsonObject{
                         {"status", "success"},
                         {"data", QJsonObject{{"2062430", QJsonObject{{"appid", "2062430"}}}}}})
                     .toJson(QJsonDocument::Compact)});
            Catalog::promoteKeyedApps(keyed, steamcmd);
            PSYCHE_CHECK(!keyed.apps.contains("2062430"));
        }
        auto twoArguments = readLuaPackage("addappid(2062430, 1)\naddappid(2062431, 1, \"" +
                                           QByteArray(64, 'a') + "\")");
        PSYCHE_CHECK(twoArguments.apps == QSet<QString>{"2062430"} &&
                     twoArguments.depots == QSet<QString>{"2062431"});
        auto writeConfig = [](const QString& directory, const QByteArray& text) {
            PSYCHE_CHECK(QDir().mkpath(directory));
            QFile file(directory + "/config.yaml");
            PSYCHE_CHECK(file.open(QIODevice::WriteOnly));
            PSYCHE_CHECK(file.write(text) == text.size());
        };
        auto readYaml = [](const QString& directory) {
            QFile file(directory + "/config.yaml");
            PSYCHE_CHECK(file.open(QIODevice::ReadOnly));
            return YAML::Load(file.readAll().toStdString());
        };
        {
            auto dest = tmp.path() + "/depot-only";
            writeConfig(dest, "AdditionalApps:\n  - 42\n");
            responses.append({200, "addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")"});
            responses.append({200, steamcmdDepot("2062430")});
            applyPackage(catalog.fetch("2062430"), dest);
            auto yaml = readYaml(dest);
            PSYCHE_CHECK(yaml["AdditionalApps"].size() == 1 &&
                         yaml["AdditionalApps"][0].as<int>() == 42);
            PSYCHE_CHECK(yaml["AdditionalDepots"].size() == 1 &&
                         yaml["AdditionalDepots"][0].as<int>() == 2062430);
            PSYCHE_CHECK(yaml["DecryptionKeys"]["2062430"].IsDefined());
        }
        {
            auto dest = tmp.path() + "/promoted";
            writeConfig(dest, "AdditionalApps:\n  - 42\n");
            responses.append({200, "addappid(2062430, 1, \"" + QByteArray(64, 'a') + "\")"});
            responses.append({200, steamcmdApp("2062430")});
            applyPackage(catalog.fetch("2062430"), dest);
            auto yaml = readYaml(dest);
            QSet<int> apps;
            for (auto item : yaml["AdditionalApps"])
                apps.insert(item.as<int>());
            PSYCHE_CHECK(apps == QSet<int>({42, 2062430}));
            PSYCHE_CHECK(yaml["AdditionalDepots"].size() == 1 &&
                         yaml["AdditionalDepots"][0].as<int>() == 2062430);
        }
        auto before = requests.size();
        rejects([&] { catalog.fetch("123", "invalid"); });
        PSYCHE_CHECK(requests.size() == before);
        for (const auto& body : {QByteArray("-- no supported configuration"),
                                 QByteArray("addappid(123)\nsetdepotkey(456, \"invalid\")"),
                                 QByteArray(8 * 1024 * 1024 + 1, 'x')}) {
            responses.append({200, body});
            before = requests.size();
            rejects([&] { catalog.fetch("123"); });
            PSYCHE_CHECK(requests.size() == before + 1);
        }
        responses.append({429, "quota"});
        before = requests.size();
        rejects([&] { catalog.fetch("123"); });
        PSYCHE_CHECK(requests.size() == before + 1); // No automatic ZIP fallback/retry.
        for (int status : {401, 403, 404, 429, 500, 302}) {
            responses.append({status, "error"});
            rejects([&] { catalog.search("fixture"); });
        }
        for (auto body :
             {QByteArray("invalid json"), QByteArray("{}"), QByteArray(4 * 1024 * 1024 + 1, 'x')}) {
            responses.append({200, body});
            rejects([&] { catalog.search("fixture"); });
        }
        responses.append({200, "not a zip"});
        rejects([&] { catalog.fetch("123", "zip"); });
        int count = requests.size();
        rejects([&] { catalog.fetch("0"); });
        rejects([&] { catalog.fetch("4294967296"); });
        rejects([&] { Catalog("").search("fixture"); });
        PSYCHE_CHECK(requests.size() == count);
        const auto steam = QUrl(QString("http://127.0.0.1:%1/api").arg(server.serverPort()));
        zip(tmp.path() + "/wrong_filename.zip",
            "game.lua",
            "addappid(2770330)\naddappid(400)\nsetdepotkey(2770331, \"" + QByteArray(64, 'a') +
                "\")");
        auto named = readPackage(tmp.path() + "/wrong_filename.zip");
        PSYCHE_CHECK(named.mainAppId == "2770330" && named.labels["2770330"] == "AppID 2770330");
        responses.append(
            {200,
             R"({"2770330":{"success":true,"data":{"steam_appid":2770330,"name":"Chaos Front"}}})"});
        PSYCHE_CHECK(Catalog::resolveGameName(named, steam));
        PSYCHE_CHECK(named.labels["2770330"] == "Chaos Front" &&
                     named.labels["2770331"] == "Chaos Front");
        PSYCHE_CHECK(requests.last().contains("appids=2770330") &&
                     requests.last().contains("l=english"));
        PSYCHE_CHECK(!requests.last().toLower().contains("authorization:"));
        applyPackage(named, tmp.path());
        QFile yaml(tmp.path() + "/config.yaml");
        PSYCHE_CHECK(yaml.open(QIODevice::ReadOnly));
        const auto text = yaml.readAll();
        PSYCHE_CHECK(text.contains("# Chaos Front") && !text.contains("wrong_filename"));
        PSYCHE_CHECK(named.games.first().toObject()["name"].toString() == "Chaos Front");
        for (
            const auto& body :
            {QByteArray("invalid"),
             QByteArray(R"({"2770330":{"success":false}})"),
             QByteArray(
                 R"({"2770330":{"success":true,"data":{"steam_appid":400,"name":"Wrong game"}}})")}) {
            responses.append({200, body});
            PSYCHE_CHECK(!Catalog::resolveGameName(named, steam));
            PSYCHE_CHECK(named.labels["2770330"] == "Chaos Front");
        }
        auto offline = readPackage(tmp.path() + "/wrong_filename.zip");
        responses.append({503, "unavailable"});
        PSYCHE_CHECK(!Catalog::resolveGameName(offline, steam));
        PSYCHE_CHECK(offline.labels["2770330"] == "AppID 2770330");
        std::cout << "Catalog checks passed with a local HTTP fixture.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
