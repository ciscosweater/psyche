#include "settings.h"
#include "test_helpers.h"
#include <QJsonDocument>
#include <QJsonObject>
static void put(const QString& path, const QByteArray& data) {
    PSYCHE_CHECK(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    PSYCHE_CHECK(file.open(QIODevice::WriteOnly));
    PSYCHE_CHECK(file.write(data) == data.size());
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        PSYCHE_CHECK(temp.isValid());
        qputenv("PSYCHE_HUBCAP_API_KEY", "");
        auto home = temp.path() + "/home", data = home + "/.local/share/psyche",
             config = home + "/.config", share = home + "/.local/share";
        auto root = share + "/Steam", sls = config + "/SLSsteam",
             library = temp.path() + "/Another Library";
        put(sls + "/config.yaml", "{}");
        PSYCHE_CHECK(QDir().mkpath(library + "/steamapps"));
        put(root + "/steamapps/libraryfolders.vdf",
            "\"libraryfolders\" { \"0\" { \"path\" \"" + root.toUtf8() +
                "\" } \"1\" { \"path\" \"" + library.toUtf8() + "\" } }");
        PSYCHE_CHECK(QDir().mkpath(home + "/.steam"));
        PSYCHE_CHECK(QFile::link(root, home + "/.steam/steam"));
        put(share + "/SLSsteam/SLSsteam.so", "fixture");
        AppSettings settings(home, data, config, share);
        PSYCHE_CHECK(settings.destination() == sls && settings.destinationValid());
        PSYCHE_CHECK(settings.steamDirectory() == root);
        PSYCHE_CHECK(!settings.steamRunning());
        PSYCHE_CHECK(settings.steamDirectories().size() == 1);
        PSYCHE_CHECK(settings.libraries().size() == 2);
        PSYCHE_CHECK(!settings.slsBinary().isEmpty());
        PSYCHE_CHECK(!QFile::exists(data + "/settings.json")); // Detection is read-only.
        PSYCHE_CHECK(settings.downloadContent() == "full");
        PSYCHE_CHECK(settings.setDownloadContent("basegame"));
        PSYCHE_CHECK(settings.savePreferences("fixture-key", true, "dark"));
        settings.saveNavigation(2, "Fixture Game");
        settings.saveWindow(900, 700);
        PSYCHE_CHECK(
            settings.recordApplication("fixture.zip", sls, sls + "/backups/fixture", "Apps: 123"));
        AppSettings loaded(home, data, config, share);
        PSYCHE_CHECK(loaded.downloadContent() == "basegame");
        PSYCHE_CHECK(loaded.apiKey() == "fixture-key" && loaded.theme() == "dark");
        PSYCHE_CHECK(loaded.lastTab() == 2 && loaded.lastQuery() == "Fixture Game");
        PSYCHE_CHECK(loaded.windowWidth() == 900);
        PSYCHE_CHECK(loaded.history().size() == 1);
        auto permissions = QFile::permissions(data + "/settings.json");
        PSYCHE_CHECK(!(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                                      QFileDevice::WriteGroup | QFileDevice::WriteOther)));
        PSYCHE_CHECK(loaded.markRestored(sls + "/backups/fixture"));
        PSYCHE_CHECK(loaded.history().first().toMap()["restored"].toBool());
        PSYCHE_CHECK(loaded.savePreferences("session-only", false, "light"));
        AppSettings noKey(home, data, config, share);
        PSYCHE_CHECK(noKey.apiKey().isEmpty());
        PSYCHE_CHECK(loaded.apiKey() == "session-only");
        auto flat = home + "/.var/app/com.valvesoftware.Steam/config/SLSsteam";
        put(flat + "/config.yaml", "{}");
        noKey.detectPaths();
        PSYCHE_CHECK(noKey.destination().isEmpty());
        PSYCHE_CHECK(noKey.destinations().size() == 2);
        PSYCHE_CHECK(noKey.chooseDestination(QUrl::fromLocalFile(sls)));
        PSYCHE_CHECK(noKey.destination() == sls);
        PSYCHE_CHECK(QDir(sls).removeRecursively());
        noKey.detectPaths();
        PSYCHE_CHECK(noKey.destination() == sls &&
                     !noKey.destinationValid()); // No silent fallback.
        PSYCHE_CHECK(noKey.useAutomaticPaths());
        PSYCHE_CHECK(noKey.destination() == flat);
        qputenv("PSYCHE_HUBCAP_API_KEY", "environment-fixture");
        PSYCHE_CHECK(noKey.effectiveApiKey() == "environment-fixture");
        qputenv("PSYCHE_HUBCAP_API_KEY", "");
        AppSettings firstInstance(home, data, config, share),
            secondInstance(home, data, config, share);
        PSYCHE_CHECK(
            firstInstance.recordApplication("one", flat, flat + "/backups/one", "Apps: 1"));
        PSYCHE_CHECK(
            secondInstance.recordApplication("two", flat, flat + "/backups/two", "Apps: 2"));
        firstInstance.saveNavigation(0, "new query");
        AppSettings merged(home, data, config, share);
        PSYCHE_CHECK(merged.history().size() == 3);
        // Fake /proc maps with spaces in paths; don't read the real /proc.
        auto proc = temp.path() + "/proc", systemLib = temp.path() + "/lib32",
             custom = temp.path() + "/Custom Libraries";
        put(custom + "/SLSsteam.so", "fixture");
        put(custom + "/library-inject.so", "fixture");
        put(systemLib + "/libSLSsteam.so", "fixture");
        put(systemLib + "/libSLS-library-inject.so", "fixture");
        put(proc + "/100/comm", "steam\n");
        put(proc + "/100/maps",
            "0000-1000 r-xp 0000 00:00 1 " + (custom + "/SLSsteam.so").toUtf8() +
                "\n1000-2000 r-xp 0000 00:00 2 " + (custom + "/library-inject.so").toUtf8() + "\n");
        AppSettings mapped(home, data, config, share, nullptr, proc, systemLib);
        PSYCHE_CHECK(mapped.steamRunning());
        PSYCHE_CHECK(mapped.slsBinary() == custom + "/SLSsteam.so");
        PSYCHE_CHECK(mapped.libraryInject() == custom + "/library-inject.so");
        put(proc + "/100/comm", "unrelated-process\n");
        mapped.detectPaths();
        PSYCHE_CHECK(!mapped.steamRunning());
        PSYCHE_CHECK(mapped.slsBinary() == systemLib + "/libSLSsteam.so");
        PSYCHE_CHECK(mapped.libraryInject() == systemLib + "/libSLS-library-inject.so");
        put(proc + "/100/comm", "steam\n");
        mapped.refreshSteamRunning();
        PSYCHE_CHECK(mapped.steamRunning());
        PSYCHE_CHECK(mapped.slsBinary() == systemLib + "/libSLSsteam.so");
        // Flatpak library path when there is no native/system .so.
        auto flatHome = temp.path() + "/flat-home",
             flatLib = flatHome + "/.var/app/com.valvesoftware.Steam/.local/share/SLSsteam";
        put(flatLib + "/SLSsteam.so", "fixture");
        put(flatLib + "/library-inject.so", "fixture");
        AppSettings flatSettings(
            flatHome, flatHome + "/data", flatHome + "/.config", flatHome + "/.local/share");
        PSYCHE_CHECK(flatSettings.slsBinary() == flatLib + "/SLSsteam.so");
        PSYCHE_CHECK(flatSettings.libraryInject() == flatLib + "/library-inject.so");
        // Read ASSella keys; env and saved psyche keys win. Don't write ACCELA.conf.
        auto importedHome = temp.path() + "/assella-home", importedData = importedHome + "/data",
             importedConfig = importedHome + "/xdg";
        auto assella = importedConfig + "/Tachibana Labs/ACCELA.conf";
        const QByteArray assellaBytes =
            "[General]\nmorrenus_api_key=fixture-assella-key\nother=preserve\n";
        put(assella, assellaBytes);
        AppSettings imported(importedHome, importedData, importedConfig, share);
        PSYCHE_CHECK(imported.importedKey() && imported.effectiveApiKey() == "fixture-assella-key");
        PSYCHE_CHECK(!imported.rememberKey() && !QFile::exists(importedData + "/settings.json"));
        imported.saveNavigation(1, "fixture");
        QFile savedImport(importedData + "/settings.json");
        PSYCHE_CHECK(savedImport.open(QIODevice::ReadOnly));
        PSYCHE_CHECK(!savedImport.readAll().contains("fixture-assella-key"));
        QFile assellaFile(assella);
        PSYCHE_CHECK(assellaFile.open(QIODevice::ReadOnly));
        PSYCHE_CHECK(assellaFile.readAll() == assellaBytes);
        assellaFile.close();
        qputenv("PSYCHE_HUBCAP_API_KEY", "fixture-environment");
        AppSettings environment(importedHome, importedData, importedConfig, share);
        PSYCHE_CHECK(!environment.importedKey() &&
                     environment.effectiveApiKey() == "fixture-environment");
        qputenv("PSYCHE_HUBCAP_API_KEY", "");
        PSYCHE_CHECK(imported.savePreferences("fixture-psyche", true, "dark"));
        AppSettings ownKey(importedHome, importedData, importedConfig, share);
        PSYCHE_CHECK(!ownKey.importedKey() && ownKey.effectiveApiKey() == "fixture-psyche");
        PSYCHE_CHECK(ownKey.savePreferences("", false, "dark"));
        put(assella, "[General]\nmorrenus_api_key=\n");
        auto fallback = importedHome + "/.config/Tachibana Labs/ACCELA.conf";
        put(fallback, "[General]\nmorrenus_api_key=fallback-fixture\n");
        AppSettings fallbackKey(importedHome, importedData, importedConfig, share);
        PSYCHE_CHECK(fallbackKey.importedKey() &&
                     fallbackKey.effectiveApiKey() == "fallback-fixture");
        PSYCHE_CHECK(QFile::remove(fallback));
        put(assella, "[General]\nmorrenus_api_key=bad\\nkey\n");
        AppSettings invalidKey(importedHome, importedData, importedConfig, share);
        PSYCHE_CHECK(!invalidKey.hasApiKey());
        // Keep a copy of corrupt settings.json before replacing it.
        put(data + "/settings.json", "broken");
        AppSettings corrupt(home, data, config, share);
        PSYCHE_CHECK(corrupt.settingsError());
        PSYCHE_CHECK(corrupt.savePreferences("", false, "system"));
        PSYCHE_CHECK(QDir(data).entryList({"settings.json.invalid-*"}, QDir::Files).size() == 1);
        std::cout << "Settings, permissions, persistence and path detection checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
