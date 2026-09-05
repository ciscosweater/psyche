#include "library.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
namespace {
const QString ledgerName = ".psyche-library.json";
void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}
void safePath(const QString& path) {
    QString current = "/";
    for (const auto& part : QFileInfo(path).absoluteFilePath().split('/', Qt::SkipEmptyParts)) {
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink())
            fail("Symbolic link not allowed: " + current);
    }
}
QByteArray read(const QString& path) {
    safePath(path);
    QFile f(path);
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly))
        fail("Could not read " + path);
    return f.readAll();
}
void write(const QString& path, const QByteArray& data) {
    safePath(path);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) ||
        !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        f.write(data) != data.size() || !f.commit())
        fail("Could not write " + path);
}
QSet<QString> ids(const QJsonArray& array) {
    QSet<QString> result;
    for (const auto& v : array)
        result.insert(v.toString());
    return result;
}
QJsonArray array(const QSet<QString>& values) {
    QJsonArray result;
    auto sorted = values.values();
    sorted.sort();
    for (const auto& v : sorted)
        result.append(v);
    return result;
}
QJsonObject entries(const QByteArray& text) {
    validateManagedYaml(text);
    auto docs = YAML::LoadAll(text.toStdString());
    if (docs.size() > 1)
        fail("Use only one YAML document");
    auto root = YAML::Load(text.toStdString());
    QJsonObject result;
    if (root && !root.IsNull() && !root.IsMap())
        fail("Invalid configuration");
    QSet<QString> seen;
    if (root.IsMap())
        for (auto item : root) {
            auto key = QString::fromStdString(item.first.as<std::string>());
            if (seen.contains(key))
                fail("Duplicate YAML field: " + key);
            seen.insert(key);
        }
    for (const auto& pair : QList<QPair<QString, QString>>{{"apps", "AdditionalApps"},
                                                           {"depots", "AdditionalDepots"},
                                                           {"keys", "DecryptionKeys"}}) {
        auto node = root.IsMap() ? root[pair.second.toStdString()] : YAML::Node();
        if (!node || node.IsNull()) {
            result[pair.first] =
                pair.first == "keys" ? QJsonValue(QJsonObject()) : QJsonValue(QJsonArray());
            continue;
        }
        if (pair.first == "keys") {
            if (!node.IsMap())
                fail("Invalid DecryptionKeys");
            QJsonObject keys;
            for (auto item : node) {
                auto key = QString::fromStdString(item.first.as<std::string>());
                if (keys.contains(key))
                    fail("Duplicate depot key");
                keys[key] = QString::fromStdString(item.second.as<std::string>()).toLower();
            }
            result["keys"] = keys;
        } else {
            if (!node.IsSequence())
                fail("Invalid " + pair.second);
            QSet<QString> values;
            for (auto item : node)
                values.insert(QString::fromStdString(item.as<std::string>()));
            result[pair.first] = array(values);
        }
    }
    return result;
}
QJsonObject discover(const QByteArray& text) {
    entries(text);
    auto root = YAML::Load(text.toStdString());
    QMap<QString, QJsonObject> groups;
    if (root.IsMap())
        for (const auto& pair : QList<QPair<QString, QString>>{{"apps", "AdditionalApps"},
                                                               {"depots", "AdditionalDepots"},
                                                               {"keys", "DecryptionKeys"}}) {
            auto node = root[pair.second.toStdString()];
            if (!node || node.IsNull())
                continue;
            for (auto item : node) {
                auto value = pair.first == "keys" ? item.first : YAML::Node(item);
                auto mark = value.Mark();
                int end = text.indexOf('\n', mark.pos);
                if (end < 0)
                    end = text.size();
                auto line = QString::fromUtf8(text.mid(mark.pos, end - mark.pos));
                auto comment = QRegularExpression("\\s+#\\s*(.+?)\\s*$").match(line);
                if (!comment.hasMatch())
                    continue;
                auto name = comment.captured(1).trimmed();
                auto game = groups.value(
                    name,
                    QJsonObject{{"id",
                                 QString::fromLatin1(QCryptographicHash::hash(
                                                         name.toUtf8(), QCryptographicHash::Sha256)
                                                         .toHex())},
                                {"name", name},
                                {"recovered", true}});
                auto id = QString::fromStdString(value.as<std::string>());
                if (pair.first == "keys") {
                    auto keys = game["keys"].toObject();
                    keys[id] = QString::fromStdString(item.second.as<std::string>()).toLower();
                    game["keys"] = keys;
                } else {
                    auto values = ids(game[pair.first].toArray());
                    values.insert(id);
                    game[pair.first] = array(values);
                }
                groups[name] = game;
            }
        }
    QJsonArray games;
    QJsonObject managed;
    QSet<QString> apps, depots;
    QJsonObject keys;
    for (auto game : groups) {
        for (const auto& field : {"apps", "depots"})
            game[field] = array(ids(game.value(field).toArray()));
        game["keys"] = game.value("keys").toObject();
        QString appId;
        for (const auto& value : ids(game["apps"].toArray()))
            if (appId.isEmpty() || value.toULongLong() < appId.toULongLong())
                appId = value;
        game["appId"] = appId;
        games.append(game);
        apps.unite(ids(game["apps"].toArray()));
        depots.unite(ids(game["depots"].toArray()));
        auto k = game["keys"].toObject();
        for (auto it = k.begin(); it != k.end(); ++it)
            keys[it.key()] = it.value();
    }
    managed = {{"apps", array(apps)}, {"depots", array(depots)}, {"keys", keys}};
    return {{"version", 1}, {"games", games}, {"managed", managed}};
}
void validateLedger(const QJsonObject& root) {
    auto invalid = [](const char* reason) {
        fail(QString("Invalid library metadata (%1); restore a backup before continuing")
                 .arg(reason));
    };
    auto validId = [](const QString& id) {
        return QRegularExpression("^[1-9][0-9]{0,9}$").match(id).hasMatch() &&
               id.toULongLong() <= 4294967295ULL;
    };
    auto validateEntries = [&](const QJsonObject& record) {
        for (const auto& field : {"apps", "depots"})
            if (record.contains(field) && !record[field].isNull()) {
                if (!record[field].isArray())
                    invalid("entry list");
                QSet<QString> seen;
                for (auto value : record[field].toArray()) {
                    auto id = value.toString();
                    if (!value.isString() || !validId(id) || seen.contains(id))
                        invalid("entry ID");
                    seen.insert(id);
                }
            }
        if (record.contains("keys") && !record["keys"].isNull()) {
            if (!record["keys"].isObject())
                invalid("key map");
            auto keys = record["keys"].toObject();
            for (auto it = keys.begin(); it != keys.end(); ++it)
                if (!validId(it.key()) || !it.value().isString() ||
                    !QRegularExpression("^[a-fA-F0-9]{64}$")
                         .match(it.value().toString())
                         .hasMatch())
                    invalid("depot key");
        }
    };
    if (root["version"].toInt() != 1 || !root["games"].isArray() || !root["managed"].isObject())
        invalid("schema");
    auto managed = root["managed"].toObject();
    for (const auto& field : {"apps", "depots", "keys"})
        if (!managed.contains(field) ||
            (QString(field) == "keys" ? !managed[field].isObject() : !managed[field].isArray()))
            invalid("managed entries");
    validateEntries(managed);
    QSet<QString> seen;
    for (auto value : root["games"].toArray()) {
        if (!value.isObject())
            invalid("game record");
        auto game = value.toObject();
        auto id = game["id"].toString();
        if (id.isEmpty() || seen.contains(id) || !game["name"].isString() ||
            game["name"].toString().trimmed().isEmpty())
            invalid("game identity");
        seen.insert(id);
        if (game.contains("appId") &&
            (!game["appId"].isString() ||
             (!game["appId"].toString().isEmpty() && !validId(game["appId"].toString()))))
            invalid("game AppID");
        validateEntries(game);
    }
}
QJsonObject load(const QString& directory, const QByteArray& config) {
    auto path = QDir(directory).filePath(ledgerName);
    auto bytes = read(path);
    if (!QFileInfo::exists(path))
        return discover(config);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    auto root = doc.object();
    if (error.error != QJsonParseError::NoError || root["version"].toInt() != 1 ||
        !root["games"].isArray() || !root["managed"].isObject())
        fail("Invalid library metadata; restore a backup before continuing");
    validateLedger(root);
    return root;
}
void save(const QString& directory, const QJsonObject& ledger) {
    write(QDir(directory).filePath(ledgerName), QJsonDocument(ledger).toJson());
}
void addLedgerBackup(const QString& directory, const QString& backup) {
    auto path = QDir(directory).filePath(ledgerName);
    bool exists = QFileInfo::exists(path);
    auto data = read(path);
    if (exists)
        write(backup + "/" + ledgerName, data);
    auto manifest = read(backup + "/files.txt");
    manifest += (exists ? "1 " : "0 ") + ledgerName.toUtf8() + "\n";
    write(backup + "/files.txt", manifest);
}
QString removalBackup(const QString& directory, const QByteArray& config) {
    auto backup = QDir(directory).filePath("backups/psyche-" +
                                           QUuid::createUuid().toString(QUuid::WithoutBraces));
    safePath(backup);
    if (!QDir().mkpath(backup))
        fail("Could not create backup");
    write(backup + "/config.yaml", config);
    write(backup + "/files.txt", "1 config.yaml\n");
    addLedgerBackup(directory, backup);
    return backup;
}
QByteArray removeEntries(QByteArray text, const QJsonObject& remove) {
    for (const auto& pair : QList<QPair<QString, QString>>{{"apps", "AdditionalApps"},
                                                           {"depots", "AdditionalDepots"},
                                                           {"keys", "DecryptionKeys"}}) {
        auto wanted = pair.first == "keys" ? QSet<QString>() : ids(remove[pair.first].toArray());
        auto keyValues = remove["keys"].toObject();
        if (pair.first == "keys")
            for (auto it = keyValues.begin(); it != keyValues.end(); ++it)
                wanted.insert(it.key());
        if (wanted.isEmpty())
            continue;
        auto root = YAML::Load(text.toStdString());
        auto node = root[pair.second.toStdString()];
        if (!node || node.IsNull())
            continue;
        QList<QPair<int, int>> spans;
        for (auto item : node) {
            auto value = pair.first == "keys" ? item.first : YAML::Node(item);
            auto id = QString::fromStdString(value.as<std::string>());
            if (!wanted.contains(id))
                continue;
            if (pair.first == "keys" &&
                QString::fromStdString(item.second.as<std::string>()).toLower() !=
                    keyValues[id].toString())
                fail("Depot key changed outside psyche; removal cancelled: " + id);
            int offset = value.Mark().pos, start = text.lastIndexOf('\n', offset - 1) + 1,
                end = text.indexOf('\n', offset);
            if (end < 0)
                end = text.size();
            else
                ++end;
            auto line = QString::fromUtf8(text.mid(start, end - start));
            QString token = "(?:" + QRegularExpression::escape(id) + "|'" +
                            QRegularExpression::escape(id) + "'|\"" +
                            QRegularExpression::escape(id) + "\")";
            auto pattern = pair.first == "keys" ? "^\\s*" + token +
                                                      ":\\s*(?:\"[a-fA-F0-9]{64}\"|'[a-fA-F0-9]{64}"
                                                      "'|[a-fA-F0-9]{64}),?\\s*(?:#.*)?$"
                                                : "^\\s*(?:-\\s+)?" + token + ",?\\s*(?:#.*)?$";
            if (!QRegularExpression(pattern).match(line.trimmed()).hasMatch())
                fail("Entry formatting changed outside psyche; removal cancelled: " + id);
            spans.append({start, end - start});
        }
        std::sort(spans.begin(), spans.end(), [](auto a, auto b) { return a.first > b.first; });
        for (auto span : spans)
            text.remove(span.first, span.second);
        auto updated = YAML::Load(text.toStdString());
        auto remaining = updated[pair.second.toStdString()];
        // Empty block-style fields can stay bare. Flow maps need `[]` / `{}`.
        if (updated.Style() != YAML::EmitterStyle::Flow && remaining && !remaining.IsNull() &&
            remaining.size() == 0 && remaining.Style() == YAML::EmitterStyle::Flow) {
            const int start = remaining.Mark().pos;
            int end = start + 1;
            bool comment = false;
            for (; end < text.size(); ++end) {
                if (text[end] == '\n')
                    comment = false;
                else if (text[end] == '#')
                    comment = true;
                else if (!comment && (text[end] == ']' || text[end] == '}'))
                    break;
            }
            if (end >= text.size())
                fail("Could not preserve empty YAML section");
            text.remove(end, 1);
            text.remove(start, 1);
        }
    }
    text = formatManagedYaml(text);
    entries(text);
    return text;
}
} // namespace
QString applyPackage(const Package& package, const QString& directory) {
    if (directory.trimmed().isEmpty())
        fail("Choose a destination");
    safePath(directory);
    QLockFile lock(QDir(directory).filePath(".psyche.lock"));
    if (!lock.tryLock(100))
        fail("Another operation is modifying this configuration");
    auto config = read(QDir(directory).filePath("config.yaml"));
    auto before = entries(config);
    auto ledger = load(directory, config);
    auto managed = ledger["managed"].toObject();
    for (const auto& field : {"apps", "depots"}) {
        auto owned = ids(managed[field].toArray());
        auto incoming = QString(field) == "apps" ? package.apps : package.depots;
        owned.unite(incoming - ids(before[field].toArray()));
        managed[field] = array(owned);
    }
    auto ownedKeys = managed["keys"].toObject();
    auto existingKeys = before["keys"].toObject();
    for (auto it = package.keys.begin(); it != package.keys.end(); ++it)
        if (!existingKeys.contains(it.key()))
            ownedKeys[it.key()] = it.value();
    managed["keys"] = ownedKeys;
    ledger["managed"] = managed;
    auto incoming = package.games;
    if (incoming.isEmpty()) {
        auto copy = package;
        copy.setGameName(package.labels.isEmpty() ? "Imported game" : package.labels.first());
        incoming = copy.games;
    }
    auto games = ledger["games"].toArray();
    for (auto value : incoming) {
        auto game = value.toObject();
        int found = -1;
        for (int i = 0; i < games.size(); ++i)
            if ((!game["appId"].toString().isEmpty() &&
                 games[i].toObject()["appId"] == game["appId"]) ||
                (game["appId"].toString().isEmpty() &&
                 games[i].toObject()["appId"].toString().isEmpty() &&
                 games[i].toObject()["name"] == game["name"])) {
                found = i;
                break;
            }
        if (found >= 0) {
            auto previous = games[found].toObject();
            game["id"] = previous["id"];
            for (const auto& field : {"apps", "depots"})
                game[field] = array(ids(game[field].toArray()) | ids(previous[field].toArray()));
            auto keys = previous["keys"].toObject(), more = game["keys"].toObject();
            for (auto it = more.begin(); it != more.end(); ++it)
                keys[it.key()] = it.value();
            game["keys"] = keys;
            games[found] = game;
        } else {
            game["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            games.append(game);
        }
    }
    ledger["games"] = games;
    auto backup = applyConfiguration(package, directory);
    try {
        addLedgerBackup(directory, backup);
        save(directory, ledger);
    } catch (...) {
        restoreBackupContents(directory, backup);
        throw;
    }
    return backup;
}
QVariantList installedGames(const QString& directory) {
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
        return {};
    auto config = read(QDir(directory).filePath("config.yaml"));
    auto ledger = load(directory, config);
    auto current = entries(config);
    QVariantList result;
    for (auto value : ledger["games"].toArray()) {
        auto game = value.toObject();
        auto apps = ids(game["apps"].toArray()) & ids(current["apps"].toArray());
        auto depots = ids(game["depots"].toArray()) & ids(current["depots"].toArray());
        int keys = 0;
        auto stored = game["keys"].toObject(), actual = current["keys"].toObject();
        for (auto it = stored.begin(); it != stored.end(); ++it)
            if (actual.contains(it.key()))
                ++keys;
        if (apps.isEmpty() && depots.isEmpty() && !keys)
            continue;
        auto sorted = apps.values();
        sorted.sort();
        result.append(QVariantMap{{"id", game["id"].toString()},
                                  {"name", game["name"].toString()},
                                  {"appId",
                                   game["appId"].toString().isEmpty()
                                       ? (sorted.isEmpty() ? QString() : sorted.first())
                                       : game["appId"].toString()},
                                  {"apps", apps.size()},
                                  {"depots", depots.size()},
                                  {"keys", keys}});
    }

    return result;
}
QString removeInstalledGame(const QString& directory, const QString& gameId) {
    if (directory.trimmed().isEmpty() ||
        !QFileInfo(QDir(directory).filePath("config.yaml")).isFile())
        fail("Configuration is unavailable; refresh the library");
    safePath(directory);
    QLockFile lock(QDir(directory).filePath(".psyche.lock"));
    if (!lock.tryLock(100))
        fail("Another operation is modifying this configuration");
    auto config = read(QDir(directory).filePath("config.yaml"));
    entries(config);
    auto ledger = load(directory, config);
    auto games = ledger["games"].toArray();
    QJsonObject selected;
    QJsonArray remaining;
    for (auto game : games)
        if (game.toObject()["id"].toString() == gameId)
            selected = game.toObject();
        else
            remaining.append(game);
    if (selected.isEmpty())
        fail("Game is no longer in the library; refresh and try again");
    // Recovered comments don't list depots/keys skipped as duplicates on older imports.
    bool recovered = false;
    QSet<QString> trackedApps;
    for (auto value : games) {
        auto game = value.toObject();
        recovered = recovered || game["recovered"].toBool();
        trackedApps.unite(ids(game["apps"].toArray()));
    }
    auto untrackedApps = ids(entries(config)["apps"].toArray()) - trackedApps;
    if (recovered && (!remaining.isEmpty() || !untrackedApps.isEmpty()) &&
        (!selected["depots"].toArray().isEmpty() || !selected["keys"].toObject().isEmpty()))
        fail("Re-import legacy game manifests before removing depot/key entries; their shared "
             "references are unknown.");
    QJsonObject remove, managed = ledger["managed"].toObject();
    for (const auto& field : {"apps", "depots"}) {
        auto values = ids(selected[field].toArray()) & ids(managed[field].toArray());
        for (auto other : remaining) {
            values.subtract(ids(other.toObject()[field].toArray()));
            if (QString(field) == "depots") {
                auto keys = other.toObject()["keys"].toObject();
                for (auto it = keys.begin(); it != keys.end(); ++it)
                    values.remove(it.key());
            }
        }
        remove[field] = array(values);
        managed[field] = array(ids(managed[field].toArray()) - values);
    }
    auto keys = selected["keys"].toObject(), owned = managed["keys"].toObject();
    for (const auto& depot : ids(selected["depots"].toArray()))
        if (owned.contains(depot) && !keys.contains(depot))
            keys[depot] = owned[depot];
    for (auto it = keys.begin(); it != keys.end();) {
        bool shared = false;
        for (auto other : remaining)
            if (other.toObject()["keys"].toObject().contains(it.key()) ||
                ids(other.toObject()["depots"].toArray()).contains(it.key()))
                shared = true;
        if (shared || !owned.contains(it.key()))
            it = keys.erase(it);
        else
            ++it;
    }
    remove["keys"] = keys;
    for (auto it = keys.begin(); it != keys.end(); ++it)
        owned.remove(it.key());
    managed["keys"] = owned;
    auto updated = removeEntries(config, remove);
    ledger["games"] = remaining;
    ledger["managed"] = managed;
    auto backup = removalBackup(directory, config);
    try {
        write(QDir(directory).filePath("config.yaml"), updated);
        save(directory, ledger);
    } catch (...) {
        restoreBackupContents(directory, backup);
        throw;
    }
    return backup;
}

void restoreBackup(const QString& directory, const QString& backup) {
    if (directory.trimmed().isEmpty())
        fail("Choose a destination");
    safePath(directory);
    QLockFile lock(QDir(directory).filePath(".psyche.lock"));
    if (!lock.tryLock(100))
        fail("Another operation is modifying this configuration");
    restoreBackupContents(directory, backup, true);
}
