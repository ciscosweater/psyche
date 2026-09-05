#include "library.h"
#include "test_helpers.h"
#include <yaml-cpp/yaml.h>
static void put(const QString& path, const QByteArray& data) {
    QFile f(path);
    PSYCHE_CHECK(f.open(QIODevice::WriteOnly));
    PSYCHE_CHECK(f.write(data) == data.size());
}
static QByteArray get(const QString& path) {
    QFile f(path);
    PSYCHE_CHECK(f.open(QIODevice::ReadOnly));
    return f.readAll();
}
static QString gameId(const QString& directory, const QString& name) {
    for (auto value : installedGames(directory)) {
        auto game = value.toMap();
        if (game["name"] == name)
            return game["id"].toString();
    }
    throw std::runtime_error("Missing game");
}
template <class F>
static void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    PSYCHE_CHECK(failed);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        PSYCHE_CHECK(temp.isValid());
        auto d = temp.path(), file = d + "/config.yaml";
        const QByteArray baseline = "# Keep this header\r\nPlugins: yes # keep\r\nAdditionalApps: "
                                    "[42]\r\nOther: 'unchanged'\r\n";
        put(file, baseline);
        Package first;
        first.apps = {"100", "42"};
        first.depots = {"500"};
        first.keys["500"] = QString(64, 'a');
        first.setGameName("First game");
        Package second;
        second.apps = {"200"};
        second.depots = {"500"};
        second.keys["500"] = QString(64, 'a');
        second.setGameName("Second game");
        auto initialBackup = applyPackage(first, d);
        applyPackage(second, d);
        applyPackage(first, d);
        PSYCHE_CHECK(installedGames(d).size() == 2);
        auto all = get(file);
        auto removeFirst = removeInstalledGame(d, gameId(d, "First game"));
        auto remaining = get(file);
        PSYCHE_CHECK(!remaining.contains("100, #"));
        PSYCHE_CHECK(remaining.contains("200, #"));
        PSYCHE_CHECK(remaining.contains("500:"));
        PSYCHE_CHECK(remaining.startsWith("# Keep this header\r\nPlugins: yes # keep\r\n"));
        PSYCHE_CHECK(remaining.contains("Other: 'unchanged'\r\n"));
        PSYCHE_CHECK(installedGames(d).size() == 1);
        auto removeSecond = removeInstalledGame(d, gameId(d, "Second game"));
        auto empty = get(file);
        auto yaml = YAML::Load(empty.toStdString());
        PSYCHE_CHECK(yaml["AdditionalApps"].size() == 1 &&
                     yaml["AdditionalApps"][0].as<int>() == 42);
        PSYCHE_CHECK(yaml["AdditionalDepots"].IsNull());
        PSYCHE_CHECK(yaml["DecryptionKeys"].IsNull());
        PSYCHE_CHECK(empty.contains("AdditionalDepots:\r\n") &&
                     empty.contains("DecryptionKeys:\r\n"));
        PSYCHE_CHECK(installedGames(d).isEmpty());
        restoreBackup(d, removeSecond);
        PSYCHE_CHECK(installedGames(d).size() == 1);
        restoreBackup(d, removeFirst);
        PSYCHE_CHECK(get(file) == all && installedGames(d).size() == 2);
        // Bare fields should stay empty after add+remove, without leftover `[]`/`{}`.
        QTemporaryDir bare;
        put(bare.path() + "/config.yaml",
            "AdditionalApps: # apps\n\nAdditionalDepots: # depots\n\nDecryptionKeys: # keys\n");
        applyPackage(first, bare.path());
        removeInstalledGame(bare.path(), gameId(bare.path(), "First game"));
        const auto bareText = get(bare.path() + "/config.yaml");
        const auto bareYaml = YAML::Load(bareText.toStdString());
        for (const auto field : {"AdditionalApps", "AdditionalDepots", "DecryptionKeys"})
            PSYCHE_CHECK(bareYaml[field].IsNull());
        PSYCHE_CHECK(!bareText.contains('[') && !bareText.contains('{') &&
                     bareText.contains("# depots"));
        // Refuse removal if the stored key bytes no longer match.
        auto changed = all;
        changed.replace(QByteArray(64, 'a'), QByteArray(64, 'b'));
        put(file, changed);
        auto ledger = get(d + "/.psyche-library.json");
        removeInstalledGame(d, gameId(d, "First game"));
        auto sharedRemoved = get(file);
        auto currentLedger = get(d + "/.psyche-library.json");
        rejects([&] { removeInstalledGame(d, gameId(d, "Second game")); });
        PSYCHE_CHECK(get(file) == sharedRemoved &&
                     get(d + "/.psyche-library.json") == currentLedger);
        restoreBackup(d, initialBackup);
        PSYCHE_CHECK(get(file) == baseline && !QFile::exists(d + "/.psyche-library.json"));
        // Recover games from `# Name` comments without the original Lua/ZIP.
        put(file,
            "Plugins: yes\nAdditionalApps:\n  - 321 # Legacy game\nAdditionalDepots:\n  - 654 # "
            "Legacy game\nDecryptionKeys:\n  654: \"" +
                QByteArray(64, 'a') + "\" # Legacy game\n");
        auto legacy = get(file);
        auto id = gameId(d, "Legacy game");
        PSYCHE_CHECK(id == gameId(d, "Legacy game"));
        PSYCHE_CHECK(!QFile::exists(d + "/.psyche-library.json"));
        auto backup = removeInstalledGame(d, id);
        PSYCHE_CHECK(installedGames(d).isEmpty());
        restoreBackup(d, backup);
        PSYCHE_CHECK(get(file) == legacy);
        PSYCHE_CHECK(QFile::link(d + "/missing-ledger", d + "/.psyche-library.json"));
        rejects([&] { installedGames(d); });
        // Last owner of a depot also drops a key left by a previous owner.
        QTemporaryDir shared;
        PSYCHE_CHECK(shared.isValid());
        auto sharedFile = shared.path() + "/config.yaml";
        put(sharedFile, "Plugins: yes\n");
        Package keyOwner;
        keyOwner.apps = {"800"};
        keyOwner.depots = {"900"};
        keyOwner.keys["900"] = QString(64, 'a');
        keyOwner.setGameName("Key owner");
        Package depotOwner;
        depotOwner.apps = {"801"};
        depotOwner.depots = {"900"};
        depotOwner.setGameName("Depot owner");
        applyPackage(keyOwner, shared.path());
        applyPackage(depotOwner, shared.path());
        removeInstalledGame(shared.path(), gameId(shared.path(), "Key owner"));
        PSYCHE_CHECK(get(sharedFile).contains("900:"));
        removeInstalledGame(shared.path(), gameId(shared.path(), "Depot owner"));
        PSYCHE_CHECK(YAML::Load(get(sharedFile).toStdString())["DecryptionKeys"].size() == 0);
        applyPackage(keyOwner, shared.path());
        auto validLedger = get(shared.path() + "/.psyche-library.json");
        auto stableConfig = get(sharedFile);
        for (const auto& bad : QList<QByteArray>{
                 QByteArray(), "{}", "{\"version\":1,\"games\":[{}],\"managed\":{}}"}) {
            put(shared.path() + "/.psyche-library.json", bad);
            rejects([&] { installedGames(shared.path()); });
            rejects([&] { applyPackage(depotOwner, shared.path()); });
            PSYCHE_CHECK(get(sharedFile) == stableConfig);
        }
        put(shared.path() + "/.psyche-library.json", validLedger);
        QTemporaryDir legacyShared;
        PSYCHE_CHECK(legacyShared.isValid());
        auto legacyFile = legacyShared.path() + "/config.yaml";
        put(legacyFile,
            "AdditionalApps:\n  - 101 # Older A\n  - 102 # Older B\nAdditionalDepots:\n  - 103 # "
            "Older A\n");
        auto legacyBefore = get(legacyFile);
        rejects([&] {
            removeInstalledGame(legacyShared.path(), gameId(legacyShared.path(), "Older A"));
        });
        PSYCHE_CHECK(get(legacyFile) == legacyBefore);
        // Restoring a backup from before the ledger exists must drop newer metadata.
        auto oldBackup = shared.path() + "/old-backup";
        PSYCHE_CHECK(QDir().mkpath(oldBackup));
        put(oldBackup + "/files.txt", "1 config.yaml\n");
        put(oldBackup + "/config.yaml", "AdditionalApps: []\nPlugins: yes\n");
        restoreBackup(shared.path(), oldBackup);
        PSYCHE_CHECK(!QFile::exists(shared.path() + "/.psyche-library.json"));
        PSYCHE_CHECK(installedGames(shared.path()).isEmpty());
        // Punctuation in `# Game name` stays a comment; key hex is unchanged.
        for (const QString& name : QStringList{"ReStory: Chill Electronics Repairs",
                                               "Game #2: [yes] {no} &anchor *alias",
                                               QString::fromUtf8("Ação: 日本語 \"quoted\""),
                                               "Injected\nOther: changed"}) {
            for (const QString& hex : QStringList{QString(64, '0'),
                                                  QString(64, '9'),
                                                  QString(63, '0') + "e",
                                                  QString(64, 'a')}) {
                QTemporaryDir edge;
                const auto path = edge.path() + "/config.yaml";
                const QByteArray initial = "Other: |+\r\n  untouched: # literal\r\n\r\n";
                put(path, initial);
                Package game;
                game.apps = {"3812600"};
                game.depots = {"3812601"};
                game.keys["3812601"] = hex;
                game.setGameName(name);
                const auto saved = applyPackage(game, edge.path());
                const auto added = get(path);
                auto parsed = YAML::Load(added.toStdString());
                PSYCHE_CHECK(QString::fromStdString(
                                 parsed["DecryptionKeys"]["3812601"].as<std::string>()) == hex);
                PSYCHE_CHECK(parsed.size() == 5 &&
                             added.contains(("3812601: " + hex + " # ").toUtf8()));
                applyPackage(game, edge.path());
                PSYCHE_CHECK(get(path) == added);
                removeInstalledGame(edge.path(), gameId(edge.path(), name));
                PSYCHE_CHECK(installedGames(edge.path()).isEmpty());
                restoreBackup(edge.path(), saved);
                PSYCHE_CHECK(get(path) == initial);
            }
        }
        for (const QByteArray& aliased :
             QList<QByteArray>{"Other: &list\n  - 123\nAdditionalApps: *list\n",
                               "AdditionalApps: &list\n  - 123\nOther: *list\n",
                               "AdditionalApps:\n  - &app 123\nOther: *app\n",
                               "AdditionalApps: !!seq [123]\n",
                               "AdditionalApps: [123]\n---\nOther: keep\n"}) {
            QTemporaryDir edge;
            const auto path = edge.path() + "/config.yaml";
            put(path, aliased);
            rejects([&] { applyPackage(first, edge.path()); });
            PSYCHE_CHECK(get(path) == aliased &&
                         !QFile::exists(edge.path() + "/.psyche-library.json"));
        }
        std::cout << "Library: shared ownership, baseline preservation, targeted removal, legacy "
                     "recovery and restore passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
