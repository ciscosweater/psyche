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
template <class F>
static void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    PSYCHE_CHECK(rejected);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    PSYCHE_CHECK(tmp.isValid());
    auto d = tmp.path();
    try {
        zip(d + "/lexer.zip",
            "game.lua",
            R"lua(
local a = "addappid(999)"
local b = 'setdepotkey(888, "aaa")'
local c = [==[ addappid(777) ]==]
-- addappid(666)
--[=[addappid(555)]=]
notaddappid(444)
obj.addappid(333)
obj:addappid(222)
addappid(123) -- real entry
addappid -- comment between tokens
(456)
)lua" + QByteArray("\nsetdepotkey(789, \"") +
                QByteArray(64, 'a') + "\")");
        rejects([&] { applyPackage(Package{}, QString()); });
        rejects([&] { restoreBackup(QString(), d); });
        auto p = readPackage(d + "/lexer.zip");
        PSYCHE_CHECK(p.apps == QSet<QString>({"123", "456"}));
        PSYCHE_CHECK(p.keys.contains("789"));
        put(d + "/config.yaml", "Plugins: no\nUnrelated: keep\n");
        auto backup = applyPackage(p, d);
        PSYCHE_CHECK(
            YAML::Load(get(d + "/config.yaml").toStdString())["Plugins"].as<std::string>() ==
            "yes");
        restoreBackup(d, backup);
        PSYCHE_CHECK(get(d + "/config.yaml") == "Plugins: no\nUnrelated: keep\n");
        PSYCHE_CHECK(QDir().mkdir(d + "/outside"));
        PSYCHE_CHECK(QFile::link(d + "/outside", d + "/plugins"));
        put(d + "/outside/download.lua", "preserve");
        backup = applyPackage(p, d);
        restoreBackup(d, backup);
        PSYCHE_CHECK(get(d + "/outside/download.lua") == "preserve");
        put(d + "/config.yaml", "Other: keep\r\n");
        applyPackage(p, d);
        auto spaced = get(d + "/config.yaml");
        for (const auto field :
             {"AdditionalApps:", "AdditionalDepots:", "DecryptionKeys:", "Plugins: yes"})
            PSYCHE_CHECK(spaced.contains(QByteArray("\r\n\r\n") + field));
        // Keep comments, indent, CRLF, quotes, and `...`.
        p.setGameName("Portal 2\nInjected: no");
        const QByteArray original =
            "# Unicode comment — preserved\r\nPlugins: 'no' # keep\r\nAdditionalApps: # apps\r\n   "
            " - 42 # old\r\nAdditionalDepots: [12] # depots\r\nDecryptionKeys: {} # "
            "keys\r\nUnrelated: |\r\n  exact text\r\n...\r\n";
        put(d + "/config.yaml", original);
        backup = applyPackage(p, d);
        auto patched = get(d + "/config.yaml");
        PSYCHE_CHECK(patched.contains("    - 42 # old\r\n"));
        PSYCHE_CHECK(patched.contains("Plugins: 'yes' # keep\r\n"));
        PSYCHE_CHECK(patched.contains("Unrelated: |\r\n  exact text\r\n...\r\n"));
        PSYCHE_CHECK(patched.contains("    - 123 # Portal 2 Injected: no\r\n"));
        PSYCHE_CHECK(YAML::Load(patched.toStdString())["AdditionalApps"].size() == 3);
        applyPackage(p, d);
        PSYCHE_CHECK(get(d + "/config.yaml") == patched);
        restoreBackup(d, backup);
        PSYCHE_CHECK(get(d + "/config.yaml") == original);
        put(d + "/config.yaml", "{Other: 'keep'} # root comment\n");
        applyPackage(p, d);
        PSYCHE_CHECK(get(d + "/config.yaml").contains("Other: 'keep'} # root comment\n"));
        put(d + "/config.yaml", "# header\n...\n");
        applyPackage(p, d);
        PSYCHE_CHECK(get(d + "/config.yaml").endsWith("...\n"));
        put(d + "/config.yaml", "AdditionalApps: []\nAdditionalApps: []\n");
        rejects([&] { applyPackage(p, d); });
        zip(d + "/ignored.zip", "download.lua", "addappid(999)");
        rejects([&] { readPackage(d + "/ignored.zip"); });
        // Plugins: yes in place; don't add a second Plugins field.
        for (const auto& original : QList<QByteArray>{"Plugins: yes # keep\n",
                                                      "Plugins: \"no\" # keep\n",
                                                      "{Plugins: no, Other: 'keep'}\n",
                                                      "# header\n...\n"}) {
            put(d + "/config.yaml", original);
            auto restore = applyPackage(p, d);
            auto updated = get(d + "/config.yaml");
            PSYCHE_CHECK(YAML::Load(updated.toStdString())["Plugins"].as<std::string>() == "yes");
            PSYCHE_CHECK(updated.count("Plugins:") == 1);
            applyPackage(p, d);
            PSYCHE_CHECK(get(d + "/config.yaml") == updated);
            restoreBackup(d, restore);
            PSYCHE_CHECK(get(d + "/config.yaml") == original);
        }
        // Empty option values, including Plugins, are filled in.
        put(d + "/config.yaml",
            "Plugins: # keep\r\nAdditionalApps: null # apps\r\nAdditionalDepots: ~ # "
            "depots\r\nDecryptionKeys: # keys\r\n");
        applyPackage(p, d);
        auto filled = get(d + "/config.yaml");
        PSYCHE_CHECK(filled.contains("Plugins: yes # keep"));
        PSYCHE_CHECK(filled.contains("# apps\r\n") && filled.contains("# depots\r\n") &&
                     filled.contains("# keys\r\n"));
        PSYCHE_CHECK(YAML::Load(filled.toStdString())["AdditionalApps"].size() == 2);
        PSYCHE_CHECK(!filled.contains('[') && !filled.contains('{'));
        PSYCHE_CHECK(filled.contains("  - 123 #"));
        // Convert old multiline flow even when every ID is already present.
        const QByteArray oldFlow = "Other: keep\r\n\r\nAdditionalApps: [\r\n  123, # Existing "
                                   "game\r\n]\r\n\r\nAdditionalDepots: [\r\n  789, # Existing "
                                   "game\r\n]\r\n\r\nDecryptionKeys: {\r\n  789: \"" +
                                   QByteArray(64, 'a') + "\", # Existing game\r\n}\r\n";
        Package repair;
        repair.apps = {"123"};
        repair.depots = {"789"};
        repair.keys["789"] = QString(64, 'a');
        repair.setGameName("Existing game");
        put(d + "/config.yaml", oldFlow);
        auto repairBackup = applyPackage(repair, d);
        auto repaired = get(d + "/config.yaml");
        PSYCHE_CHECK(!repaired.contains('[') && !repaired.contains('{') &&
                     !repaired.contains(", #"));
        PSYCHE_CHECK(repaired.contains("  - 789 # Existing game\r\n") &&
                     repaired.contains("  789: " + QByteArray(64, 'a') + " # Existing game\r\n"));
        PSYCHE_CHECK(repaired.startsWith("Other: keep\r\n\r\n"));
        applyPackage(repair, d);
        PSYCHE_CHECK(get(d + "/config.yaml") == repaired);
        restoreBackup(d, repairBackup);
        PSYCHE_CHECK(get(d + "/config.yaml") == oldFlow);
        // Spacing: LF and CRLF, missing/extra blank lines, idempotent format.
        for (const QByteArray newline : {QByteArray("\n"), QByteArray("\r\n")})
            for (int gap : {0, 1, 3}) {
                QByteArray input =
                    "AdditionalApps:  " + newline + newline + "  - 123 # Game" + newline;
                for (int j = 0; j < gap; ++j)
                    input += newline;
                input += "AdditionalDepots:" + newline + newline + "  - 789 # Game" + newline;
                for (int j = 0; j < gap; ++j)
                    input += newline;
                input += "DecryptionKeys:" + newline + newline + "  789: \"" + QByteArray(64, 'a') +
                         "\" # Game" + newline + newline + newline;
                auto expected = "AdditionalApps:" + newline + "  - 123 # Game" + newline + newline +
                                "AdditionalDepots:" + newline + "  - 789 # Game" + newline +
                                newline + "DecryptionKeys:" + newline +
                                "  789: " + QByteArray(64, 'a') + " # Game" + newline;
                auto formatted = formatManagedYaml(input);
                PSYCHE_CHECK(formatted == expected);
                PSYCHE_CHECK(formatManagedYaml(formatted) == formatted);
                PSYCHE_CHECK(formatManagedYaml(input + "... # end" + newline) ==
                             expected + newline + "... # end" + newline);
            }
        const QByteArray scalar = "Other: |+\n  literal\n\n\nAdditionalApps:\n  - 123 # Game\n";
        PSYCHE_CHECK(formatManagedYaml(scalar) == scalar);
        // Symlink in an ancestor path is also rejected.
        PSYCHE_CHECK(QFile::link(d + "/outside", d + "/alias"));
        PSYCHE_CHECK(QDir().mkdir(d + "/outside/nested"));
        rejects([&] { applyPackage(p, d + "/alias/nested"); });
        // Bad key literals error out instead of being skipped.
        zip(d + "/invalid.zip", "game.lua", "addappid(123)\nsetdepotkey(789, \"not-a-key\")");
        rejects([&] { readPackage(d + "/invalid.zip"); });
        std::cout << "Regression checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
