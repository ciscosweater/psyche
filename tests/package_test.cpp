#include "test_helpers.h"
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    PSYCHE_CHECK(tmp.isValid());
    auto d = tmp.path();
    try {
        QByteArray original = "# fictitious test data\nUnrelated: keep\nAdditionalApps: [123]\n";
        QFile f(d + "/config.yaml");
        PSYCHE_CHECK(f.open(QIODevice::WriteOnly));
        f.write(original);
        f.close();
        zip(d + "/sample.zip",
            "game.lua",
            "addappid(456)\naddappid(789, 1, \"" + QByteArray(64, 'a') + "\")");
        auto p = readPackage(d + "/sample.zip");
        PSYCHE_CHECK(p.apps.contains("456") && p.keys.contains("789"));
        auto backup = applyPackage(p, d);
        PSYCHE_CHECK(f.open(QIODevice::ReadOnly));
        auto first = f.readAll();
        f.close();
        PSYCHE_CHECK(first.contains("keep") && first.contains("123") && first.contains("456"));
        applyPackage(p, d);
        PSYCHE_CHECK(f.open(QIODevice::ReadOnly));
        PSYCHE_CHECK(first == f.readAll());
        f.close();
        Package conflict = p;
        conflict.keys["789"] = QString(64, 'b');
        bool rejected = false;
        try {
            applyPackage(conflict, d);
        } catch (...) {
            rejected = true;
        }
        PSYCHE_CHECK(rejected);
        restoreBackup(d, backup);
        PSYCHE_CHECK(f.open(QIODevice::ReadOnly));
        PSYCHE_CHECK(f.readAll() == original);
        f.close();
        zip(d + "/bad.zip", "../outside.lua", "addappid(456)");
        rejected = false;
        try {
            readPackage(d + "/bad.zip");
        } catch (...) {
            rejected = true;
        }
        PSYCHE_CHECK(rejected);
        zip(d + "/plugin.zip", "download.lua", "-- inert fictional test plugin");
        rejected = false;
        try {
            readPackage(d + "/plugin.zip");
        } catch (...) {
            rejected = true;
        }
        PSYCHE_CHECK(rejected);
        std::cout << "All tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
