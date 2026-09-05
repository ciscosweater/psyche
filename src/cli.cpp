#include "cli.h"
#include "catalog.h"
#include "settings.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <stdexcept>
namespace {
void merge(Package& target, const Package& source) {
    for (const auto& game : source.games)
        target.games.append(game);
    target.apps.unite(source.apps);
    target.depots.unite(source.depots);
    for (auto it = source.keys.begin(); it != source.keys.end(); ++it) {
        if (target.keys.contains(it.key()) && target.keys[it.key()] != it.value())
            throw std::runtime_error("Conflicting keys between inputs.");
        target.keys[it.key()] = it.value();
    }
    for (auto it = source.labels.begin(); it != source.labels.end(); ++it)
        target.labels[it.key()] = it.value();
}
} // namespace
int runCli() {
    QTextStream out(stdout), err(stderr);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Import ZIPs or fetch AppIDs from Hubcap. Without --apply, only show a preview.\nAPI key: "
        "saved preferences or PSYCHE_HUBCAP_API_KEY.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"health", "Check Hubcap service health (free, no key required)."});
    parser.addOption({"stats", "Show Hubcap account usage and limits (free)."});
    parser.addOption({"paths", "Show saved and detected paths without changing files."});
    parser.addOption({"cli", "Run without a graphical interface."});
    parser.addOption({"zip", "Local ZIP (repeatable).", "file"});
    parser.addOption(
        {"content", "Remote content: full, basegame, dlc or zip (default: full).", "type", "full"});
    parser.addOption({"appid", "Download by AppID; uses daily quota (repeatable).", "id"});
    parser.addOption({"search", "Search Hubcap for games by name.", "name"});
    parser.addOption({"offset", "Search offset (up to 100 results per page).", "n", "0"});
    parser.addOption(
        {{"d", "destination"}, "SLSsteam directory (default: saved or detected).", "directory"});
    parser.addOption({"name", "Game name in YAML comments (one input per run).", "name"});
    parser.addOption({"apply", "Apply entries to destination with a backup."});
    parser.addOption({"restore", "Restore backup to an explicit destination.", "backup-directory"});
    parser.addPositionalArgument("input", "Local ZIPs or AppIDs.", "[input…]");
    if (!parser.parse(QCoreApplication::arguments())) {
        err << parser.errorText() << '\n';
        return 2;
    }
    if (parser.isSet("help")) {
        out << parser.helpText();
        return 0;
    }
    if (parser.isSet("version")) {
        out << "psyche " << QCoreApplication::applicationVersion() << '\n';
        return 0;
    }
    auto zips = parser.values("zip"), ids = parser.values("appid");
    for (const auto& input : parser.positionalArguments()) {
        if (QRegularExpression("^[0-9]+$").match(input).hasMatch())
            ids.append(input);
        else
            zips.append(input);
    }
    ids.removeDuplicates(); // One Hubcap download per AppID.
    if (parser.isSet("content") && ids.isEmpty()) {
        err << "--content requires an AppID.\n";
        return 2;
    }
    try {
        Catalog::contentLabel(parser.value("content"));
    } catch (const std::exception& e) {
        err << e.what() << '\n';
        return 2;
    }
    if (parser.isSet("name") &&
        (zips.size() + ids.size() != 1 || parser.value("name").trimmed().isEmpty())) {
        err << "--name requires exactly one input and a name.\n";
        return 2;
    }
    bool inputs = !zips.isEmpty() || !ids.isEmpty();
    bool paths = parser.isSet("paths");
    bool health = parser.isSet("health"), stats = parser.isSet("stats");
    if (health || stats) {
        if (inputs || paths || parser.isSet("apply") || parser.isSet("restore") ||
            parser.isSet("search") || parser.isSet("destination") || parser.isSet("offset")) {
            err << "Use --health/--stats without import, search, or restore options.\n";
            return 2;
        }
        try {
            AppSettings settings;
            Catalog catalog(settings.effectiveApiKey());
            QVariantMap result;
            if (health)
                result["health"] = catalog.health();
            if (stats)
                result["account"] = catalog.stats();
            out << QJsonDocument(QJsonObject::fromVariantMap(result)).toJson();
            return 0;
        } catch (const std::exception& e) {
            err << "Error: " << e.what() << '\n';
            return 1;
        }
    }
    bool search = parser.isSet("search"), restore = parser.isSet("restore"),
         apply = parser.isSet("apply");
    if ((paths && (search || restore || inputs || apply || parser.isSet("destination"))) ||
        (search && (inputs || restore || apply || parser.isSet("destination"))) ||
        (restore && (inputs || apply)) || (!search && parser.isSet("offset")) ||
        (restore && parser.value("destination").isEmpty()) ||
        (parser.isSet("destination") && !apply && !restore) ||
        (!paths && !search && !restore && !inputs)) {
        err << "Invalid combination. See --help; use --apply to write or --paths to inspect "
               "paths.\n";
        return 2;
    }
    bool validOffset = false;
    int offset = parser.value("offset").toInt(&validOffset);
    if (search && (!validOffset || offset < 0)) {
        err << "Invalid offset.\n";
        return 2;
    }
    try {
        AppSettings settings;
        if (paths) {
            out << "psyche data: " << settings.dataDirectory() << '\n'
                << "SLSsteam destination: "
                << (settings.destination().isEmpty() ? "Not found or ambiguous"
                                                     : settings.destination())
                << '\n';
            out << "Steam: " << settings.steamDirectory() << '\n'
                << "SLSsteam.so: " << settings.slsBinary() << '\n';
            out << "library-inject.so: " << settings.libraryInject() << '\n';
            out << "Backups: " << settings.backupDirectory() << '\n';
            for (const auto& entry : settings.destinations())
                out << "SLSsteam candidate: " << entry.toMap()["path"].toString() << '\n';
            for (const auto& entry : settings.libraries())
                out << "Library: " << entry.toMap()["path"].toString() << '\n';
            if (settings.settingsError())
                err << settings.message() << '\n';
            return 0;
        }
        Catalog catalog(settings.effectiveApiKey());
        if (search) {
            auto page = catalog.search(parser.value("search"), offset);
            for (const auto& game : page.games) {
                auto row = game.toMap();
                out << row["appId"].toString() << '\t' << row["name"].toString() << '\n';
            }
            if (page.games.isEmpty())
                out << "No games found.\n";
            if (page.hasMore)
                out << "More results available; use --offset " << offset + 100 << ".\n";
            return 0;
        }
        auto destination = parser.isSet("destination")
                               ? QDir(parser.value("destination")).absolutePath()
                               : settings.destination();
        if (apply && (destination.isEmpty() || (parser.isSet("destination") &&
                                                parser.value("destination").trimmed().isEmpty()))) {
            err << "Destination missing or ambiguous. Use --destination DIRECTORY or save a "
                   "directory in the app.\n";
            return 2;
        }
        if (restore) {
            restoreBackup(destination, QFileInfo(parser.value("restore")).absoluteFilePath());
            settings.markRestored(QFileInfo(parser.value("restore")).absoluteFilePath());
            out << "Backup restored.\n";
            return 0;
        }
        for (const auto& id : ids)
            Catalog::validateAppId(id);
        Package package;
        QList<Package> loaded;
        for (const auto& zip : zips) {
            auto input = readPackage(zip);
            Catalog::promoteKeyedApps(input);
            loaded.append(input);
        }
        for (const auto& id : ids)
            loaded.append(catalog.fetch(id, parser.value("content")));
        for (auto& input : loaded) {
            if (parser.isSet("name"))
                input.setGameName(parser.value("name"));
            else
                Catalog::resolveGameName(input);
            merge(package, input);
        }
        for (const auto& game : package.games)
            out << game.toObject()["name"].toString() << '\n';
        out << package.summary() << '\n';
        if (apply) {
            out << "Destination: " << destination << '\n';
            auto backup = applyPackage(package, destination);
            out << "Applied. Backup: " << backup << '\n';
            QStringList sources;
            for (const auto& zip : zips)
                sources << QFileInfo(zip).fileName();
            for (const auto& id : ids)
                sources << "AppID " + id + " • " + Catalog::contentLabel(parser.value("content"));
            const auto coverId = ids.size() == 1            ? ids.first()
                                 : package.apps.size() == 1 ? *package.apps.begin()
                                                            : QString();
            if (!settings.recordApplication(
                    sources.join(", "), destination, backup, package.summary(), coverId))
                err << "Warning: " << settings.message() << '\n';
        } else
            out << "Preview only. To write, use --apply --destination DIRECTORY.\n";
        return 0;
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << '\n';
        return 1;
    }
}
