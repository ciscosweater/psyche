#include "settings.h"
#include "test_helpers.h"
#include <QProcess>
#include <QProcessEnvironment>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    try {
        PSYCHE_CHECK(argc == 2 && tmp.isValid());
        auto d = tmp.path();
        zip(d + "/sample.zip", "game.lua", "addappid(123)");
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("HOME", d + "/home");
        env.insert("XDG_CONFIG_HOME", d + "/home/.config");
        env.insert("XDG_DATA_HOME", d + "/home/.local/share");
        env.insert("PSYCHE_DATA_DIR", d + "/data");
        env.remove("DISPLAY");
        env.remove("WAYLAND_DISPLAY");
        env.remove("QT_QPA_PLATFORM");
        env.remove("PSYCHE_HUBCAP_API_KEY");
        process.setProcessEnvironment(env);
        QByteArray output;
        auto run = [&](QStringList args, int code) {
            if (code == 0 && args.contains(d + "/sample.zip"))
                args << "--name" << "Fixture Game";
            process.start(argv[1], args);
            PSYCHE_CHECK(process.waitForFinished(10000));
            output = process.readAllStandardOutput() + process.readAllStandardError();
            if (process.exitCode() != code)
                std::cerr << output.toStdString();
            PSYCHE_CHECK(process.exitStatus() == QProcess::NormalExit &&
                         process.exitCode() == code);
        };
        run({"--paths"}, 0);
        PSYCHE_CHECK(output.contains("psyche data"));
        PSYCHE_CHECK(!QFile::exists(d + "/data/settings.json"));
        run({"--help"}, 0);
        PSYCHE_CHECK(output.contains("--appid") && output.contains("--health") &&
                     output.contains("--stats"));
        run({"--stats"}, 1);
        PSYCHE_CHECK(output.contains("PSYCHE_HUBCAP_API_KEY"));
        run({"--health", "--apply"}, 2);
        run({d + "/sample.zip"}, 0);
        PSYCHE_CHECK(output.contains("123"));
        PSYCHE_CHECK(!QFile::exists(d + "/config.yaml"));
        run({"--cli", "--zip", d + "/sample.zip", "--apply", "--destination", d}, 0);
        PSYCHE_CHECK(QFile::exists(d + "/config.yaml"));
        auto backups = QDir(d + "/backups").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        PSYCHE_CHECK(backups.size() == 1);
        run({"--restore", d + "/backups/" + backups.first(), "--destination", d}, 0);
        PSYCHE_CHECK(!QFile::exists(d + "/config.yaml"));
        run({"--apply", d + "/sample.zip"}, 2);
        run({"--cli"}, 2);
        run({"--search", "fixture", d + "/sample.zip"}, 2);
        run({"--destination", d, d + "/sample.zip"}, 2);
        run({"--search", "fixture", "--offset", "-1"}, 2);
        run({"--appid", "123", "--content", "unknown"}, 2);
        run({"--zip", d + "/sample.zip", "--content", "full"}, 2);
        run({"--appid", "123", "--content", "basegame"}, 1);
        PSYCHE_CHECK(output.contains("PSYCHE_HUBCAP_API_KEY"));
        run({"123"}, 1);
        PSYCHE_CHECK(output.contains("PSYCHE_HUBCAP_API_KEY"));
        run({"--appid", "4294967296"}, 1);
        PSYCHE_CHECK(output.contains("AppID"));
        run({"--zip", d + "/missing.zip"}, 1);
        // If a later input fails, the destination YAML is unchanged.
        run({"--zip",
             d + "/sample.zip",
             "--zip",
             d + "/missing.zip",
             "--apply",
             "--destination",
             d},
            1);
        PSYCHE_CHECK(!QFile::exists(d + "/config.yaml"));
        auto automatic = d + "/home/.config/SLSsteam";
        PSYCHE_CHECK(QDir().mkpath(automatic));
        QFile config(automatic + "/config.yaml");
        PSYCHE_CHECK(config.open(QIODevice::WriteOnly));
        config.write("{}");
        config.close();
        run({"--zip", d + "/sample.zip", "--apply"}, 0);
        PSYCHE_CHECK(output.contains(automatic.toUtf8()));
        auto manual = d + "/manual";
        PSYCHE_CHECK(QDir().mkpath(manual));
        AppSettings preferences(
            d + "/home", d + "/data", d + "/home/.config", d + "/home/.local/share");
        PSYCHE_CHECK(preferences.chooseDestination(QUrl::fromLocalFile(manual)));
        run({"--zip", d + "/sample.zip", "--apply"}, 0);
        PSYCHE_CHECK(QFile::exists(manual + "/config.yaml"));
        std::cout << "CLI checks passed without a display or network.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
