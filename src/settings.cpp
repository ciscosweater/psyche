#include "settings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <unistd.h>
namespace {
QString envPath(const char* name, const QString& fallback) {
    auto value = qEnvironmentVariable(name);
    return QDir::isAbsolutePath(value) ? QDir::cleanPath(value) : fallback;
}
QString existingDirectory(const QString& path) {
    QFileInfo f(path);
    return f.isDir() ? f.canonicalFilePath() : QString();
}
void appendDirectory(QVariantList& list,
                     const QString& path,
                     const QString& label,
                     const QString& marker = {}) {
    auto canonical = existingDirectory(path);
    if (canonical.isEmpty() ||
        (!marker.isEmpty() &&
         !(marker == "config.yaml" ? QFileInfo(canonical + "/" + marker).isFile()
                                   : QFileInfo(canonical + "/" + marker).isDir())))
        return;
    for (const auto& item : list)
        if (item.toMap().value("path") == canonical)
            return;
    list.append(QVariantMap{{"path", canonical}, {"label", label}});
}
bool steamProcessRunning(const QString& procRoot) {
    if (procRoot.isEmpty())
        return false;
    for (const auto& entry : QDir(procRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool pid = false;
        entry.fileName().toULongLong(&pid);
        if (!pid || entry.ownerId() != getuid())
            continue;
        QFile comm(entry.filePath() + "/comm");
        if (comm.open(QIODevice::ReadOnly) && comm.read(64).trimmed() == "steam")
            return true;
    }
    return false;
}
} // namespace
AppSettings::AppSettings(QObject* parent)
    : AppSettings(QDir::homePath(),
                  envPath("PSYCHE_DATA_DIR", QDir::homePath() + "/.local/share/psyche"),
                  envPath("XDG_CONFIG_HOME", QDir::homePath() + "/.config"),
                  envPath("XDG_DATA_HOME", QDir::homePath() + "/.local/share"),
                  parent,
                  "/proc",
                  "/usr/lib32") {}
AppSettings::AppSettings(QString home,
                         QString data,
                         QString config,
                         QString xdgData,
                         QObject* parent,
                         QString procRoot,
                         QString systemLib)
    : QObject(parent), m_home(home), m_data(data), m_config(config), m_xdgData(xdgData),
      m_procRoot(procRoot), m_systemLib(systemLib) {
    QFile file(m_data + "/settings.json");
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            m_unreadable = true;
            reportError("Could not read preferences. Check file permissions.");
        } else {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject())
                m_values = doc.object();
            else {
                m_corrupt = true;
                reportError("Invalid preferences. A copy will be preserved on the next save.");
            }
        }
    }
    m_sessionKey = m_values.value("apiKey").toString().trimmed();
    importAssellaKey();
    detectPaths();
}
void AppSettings::importAssellaKey() {
    if (!m_sessionKey.isEmpty() || environmentKey() || m_unreadable)
        return;
    // ASSella stores this key as QSettings("Tachibana Labs", "ACCELA").
    QStringList candidates{m_config + "/Tachibana Labs/ACCELA.conf",
                           m_home + "/.config/Tachibana Labs/ACCELA.conf"};
    candidates.removeDuplicates();
    for (const auto& path : candidates) {
        QFile file(path);
        if (!QFileInfo(path).isFile() || file.size() > 1024 * 1024 ||
            !file.open(QIODevice::ReadOnly))
            continue;
        file.close();
        QSettings source(path, QSettings::IniFormat);
        source.setFallbacksEnabled(false);
        const auto key = source.value("morrenus_api_key").toString().trimmed();
        if (source.status() != QSettings::NoError || key.isEmpty() || key.size() > 8192 ||
            key.contains('\r') || key.contains('\n'))
            continue;
        m_sessionKey = key;
        m_importedKey = true;
        return;
    }
}
bool AppSettings::destinationValid() const {
    return !m_destination.isEmpty() && QFileInfo(m_destination).isDir();
}
QString AppSettings::pluginDirectory() const {
    return m_destination.isEmpty() ? QString() : m_destination + "/plugins";
}
QString AppSettings::backupDirectory() const {
    return m_destination.isEmpty() ? QString() : m_destination + "/backups";
}
bool AppSettings::environmentKey() const {
    return !qEnvironmentVariable("PSYCHE_HUBCAP_API_KEY").trimmed().isEmpty();
}
QString AppSettings::effectiveApiKey() const {
    return environmentKey() ? qEnvironmentVariable("PSYCHE_HUBCAP_API_KEY").trimmed()
                            : m_sessionKey;
}
QUrl AppSettings::importDirectory() const {
    auto saved = m_values.value("importDirectory").toString();
    if (!QFileInfo(saved).isDir())
        saved = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QFileInfo(saved).isDir())
        saved = m_home;
    return QUrl::fromLocalFile(saved);
}
QVariantList AppSettings::history() const {
    return m_values.value("history").toArray().toVariantList();
}
void AppSettings::detectPaths() {
    m_destinations.clear();
    m_steams.clear();
    m_libraries.clear();
    m_slsBinary.clear();
    m_libraryInject.clear();
    m_steamRunning = steamProcessRunning(m_procRoot);
    appendDirectory(
        m_destinations, m_config + "/SLSsteam", "SLSsteam • user configuration", "config.yaml");
    appendDirectory(
        m_destinations, m_home + "/.config/SLSsteam", "SLSsteam • native", "config.yaml");
    const auto flat = m_home + "/.var/app/com.valvesoftware.Steam";
    appendDirectory(m_destinations, flat + "/config/SLSsteam", "SLSsteam • Flatpak", "config.yaml");
    appendDirectory(
        m_destinations, flat + "/.config/SLSsteam", "SLSsteam • Flatpak", "config.yaml");
    appendDirectory(m_destinations,
                    m_home + "/snap/steam/common/.config/SLSsteam",
                    "SLSsteam • Snap",
                    "config.yaml");
    auto saved = m_values.value("destination").toString();
    appendDirectory(m_destinations, saved, "Selected directory");
    // Keep a saved destination even if that path is currently missing.
    m_destination = saved.isEmpty() ? (m_destinations.size() == 1
                                           ? m_destinations.first().toMap()["path"].toString()
                                           : QString())
                                    : saved;
    for (const auto& path : QStringList{m_xdgData + "/Steam",
                                        m_home + "/.local/share/Steam",
                                        m_home + "/.steam/steam",
                                        m_home + "/.steam/root",
                                        m_home + "/.steam/debian-installation"})
        appendDirectory(m_steams, path, "Steam • native", "steamapps");
    for (const auto& path :
         QStringList{flat + "/data/Steam", flat + "/.local/share/Steam", flat + "/.steam/steam"})
        appendDirectory(m_steams, path, "Steam • Flatpak", "steamapps");
    appendDirectory(
        m_steams, m_home + "/snap/steam/common/.local/share/Steam", "Steam • Snap", "steamapps");
    auto steam = m_values.value("steamDirectory").toString();
    appendDirectory(m_steams, steam, "Steam • selected directory", "steamapps");
    m_steam = steam.isEmpty()
                  ? (m_steams.isEmpty() ? QString() : m_steams.first().toMap()["path"].toString())
                  : steam;
    const QRegularExpression paths(R"vdf("(?:path|[0-9]+)"\s*"((?:\\.|[^"\\])*)")vdf");
    for (const auto& entry : m_steams) {
        const auto root = entry.toMap()["path"].toString();
        appendDirectory(m_libraries, root, "Steam library", "steamapps");
        for (const auto& relative : {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"}) {
            QFile file(root + "/" + relative);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
                continue;
            auto matches = paths.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext()) {
                auto path = matches.next().captured(1);
                path.replace("\\\"", "\"");
                path.replace("\\\\", "\\");
                if (QDir::isAbsolutePath(path))
                    appendDirectory(m_libraries, path, "Additional library", "steamapps");
            }
        }
    }
    // Steam /proc maps can point at extra libraries. Read this user's steam
    // processes only; don't stop them or touch their environment.
    if (!m_procRoot.isEmpty()) {
        const QRegularExpression mapping(R"(^\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/.*)$)");
        for (const auto& entry :
             QDir(m_procRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool pid = false;
            entry.fileName().toULongLong(&pid);
            if (!pid || entry.ownerId() != getuid())
                continue;
            QFile comm(entry.filePath() + "/comm");
            if (!comm.open(QIODevice::ReadOnly) || comm.read(64).trimmed() != "steam")
                continue;
            QFile maps(entry.filePath() + "/maps");
            if (!maps.open(QIODevice::ReadOnly))
                continue;
            for (const auto& line : QString::fromUtf8(maps.read(4 * 1024 * 1024)).split('\n')) {
                auto match = mapping.match(line);
                if (!match.hasMatch())
                    continue;
                auto path = match.captured(1);
                path.replace("\\040", " ");
                path.replace("\\011", "\t");
                path.replace("\\012", "\n");
                path.replace("\\134", "\\");
                QFileInfo library(path);
                if (!library.isFile())
                    continue;
                if (m_slsBinary.isEmpty() &&
                    (library.fileName() == "SLSsteam.so" || library.fileName() == "libSLSsteam.so"))
                    m_slsBinary = library.canonicalFilePath();
                if (m_libraryInject.isEmpty() && (library.fileName() == "library-inject.so" ||
                                                  library.fileName() == "libSLS-library-inject.so"))
                    m_libraryInject = library.canonicalFilePath();
            }
            if (!m_slsBinary.isEmpty() && !m_libraryInject.isEmpty())
                break;
        }
    }
    QStringList slsCandidates, injectCandidates;
    if (!m_systemLib.isEmpty()) {
        slsCandidates << m_systemLib + "/libSLSsteam.so";
        injectCandidates << m_systemLib + "/libSLS-library-inject.so";
    }
    for (const auto& directory : QStringList{m_xdgData + "/SLSsteam",
                                             m_home + "/.local/share/SLSsteam",
                                             flat + "/.local/share/SLSsteam",
                                             flat + "/data/SLSsteam",
                                             m_home + "/snap/steam/common/.local/share/SLSsteam"}) {
        slsCandidates << directory + "/SLSsteam.so";
        injectCandidates << directory + "/library-inject.so"
                         << directory + "/libSLS-library-inject.so";
    }
    for (const auto& path : slsCandidates)
        if (m_slsBinary.isEmpty() && QFileInfo(path).isFile())
            m_slsBinary = QFileInfo(path).canonicalFilePath();
    for (const auto& path : injectCandidates)
        if (m_libraryInject.isEmpty() && QFileInfo(path).isFile())
            m_libraryInject = QFileInfo(path).canonicalFilePath();
    emit changed();
}
void AppSettings::refreshSteamRunning() {
    const auto running = steamProcessRunning(m_procRoot);
    if (running == m_steamRunning)
        return;
    m_steamRunning = running;
    emit changed();
}
bool AppSettings::reportError(const QString& message) {
    m_message = message;
    m_error = true;
    emit changed();
    return false;
}
bool AppSettings::persist() {
    if (m_unreadable)
        return reportError(
            "Preferences could not be read. Fix permissions and restart the app before saving.");
    if (!QDir().mkpath(m_data) ||
        !QFile::setPermissions(
            m_data, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return reportError("Could not prepare preferences directory.");
    QLockFile lock(m_data + "/settings.lock");
    if (!lock.tryLock(100))
        return reportError("Another instance is saving preferences. Try again.");
    const auto path = m_data + "/settings.json";
    QJsonObject saved;
    QFile current(path);
    if (current.exists()) {
        if (!current.open(QIODevice::ReadOnly))
            return reportError("Could not reread preferences before saving.");
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(current.readAll(), &error);
        current.close();
        if (error.error == QJsonParseError::NoError && doc.isObject())
            saved = doc.object();
        else {
            auto copy = path + ".invalid-" + QString::number(QDateTime::currentMSecsSinceEpoch());
            if (!QFile::copy(path, copy) ||
                !QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
                return reportError("Could not preserve invalid preferences.");
        }
    }
    // Merge dirty keys only so another instance's key/history isn't clobbered.
    for (const auto& key : m_dirty) {
        if (m_values.contains(key))
            saved[key] = m_values[key];
        else
            saved.remove(key);
    }
    if (m_dirty.contains("apiKey")) {
        if (rememberKey())
            saved["apiKey"] = m_sessionKey;
        else
            saved.remove("apiKey");
    }
    auto records = saved.value("history").toArray();
    for (const auto& record : m_pendingRecords) {
        auto backup = record.toObject().value("backup").toString();
        for (qsizetype i = records.size() - 1; i >= 0; --i)
            if (records[i].toObject().value("backup").toString() == backup)
                records.removeAt(i);
        records.prepend(record);
    }
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto record = records[i].toObject();
        if (m_pendingRestores.contains(record.value("backup").toString())) {
            record["restored"] = true;
            records[i] = record;
        }
    }
    while (records.size() > 100)
        records.removeLast();
    if (!records.isEmpty() || saved.contains("history"))
        saved["history"] = records;
    QSaveFile file(path);
    auto data = QJsonDocument(saved).toJson();
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(data) != data.size() || !file.commit())
        return reportError("Could not save preferences in " + m_data);
    m_values = saved;
    m_dirty.clear();
    m_pendingRecords = {};
    m_pendingRestores.clear();
    m_corrupt = false;
    m_error = false;
    m_message = "Preferences saved.";
    emit changed();
    return true;
}
bool AppSettings::chooseDestination(QUrl directory) {
    if (!directory.isLocalFile())
        return reportError("Choose a local directory.");
    auto path = existingDirectory(directory.toLocalFile());
    if (path.isEmpty())
        return reportError("Destination directory does not exist.");
    m_values["destination"] = path;
    m_dirty.insert("destination");
    detectPaths();
    return persist();
}
bool AppSettings::chooseSteam(QUrl directory) {
    if (!directory.isLocalFile())
        return reportError("Choose a local Steam directory.");
    auto path = existingDirectory(directory.toLocalFile());
    if (path.isEmpty() || !QFileInfo(path + "/steamapps").isDir())
        return reportError("Directory must contain steamapps.");
    m_values["steamDirectory"] = path;
    m_dirty.insert("steamDirectory");
    detectPaths();
    return persist();
}
bool AppSettings::useAutomaticPaths() {
    m_values.remove("destination");
    m_values.remove("steamDirectory");
    m_dirty.unite({"destination", "steamDirectory"});
    detectPaths();
    return persist();
}
QString AppSettings::downloadContent() const {
    auto value = m_values.value("downloadContent").toString("full");
    return QStringList{"full", "basegame", "dlc", "zip"}.contains(value) ? value : QString("full");
}
bool AppSettings::setDownloadContent(QString content) {
    if (!QStringList{"full", "basegame", "dlc", "zip"}.contains(content))
        return reportError("Invalid content type.");
    m_values["downloadContent"] = content;
    m_dirty.insert("downloadContent");
    return persist();
}
bool AppSettings::savePreferences(QString key, bool remember, QString theme) {
    if (!QStringList{"system", "light", "dark"}.contains(theme))
        return reportError("Invalid theme.");
    key = key.trimmed();
    if (key.contains('\r') || key.contains('\n'))
        return reportError("Invalid key.");
    if (key != m_sessionKey)
        m_importedKey = false;
    m_sessionKey = key;
    m_values["rememberKey"] = remember;
    m_values["theme"] = theme;
    m_dirty.unite({"apiKey", "rememberKey", "theme"});
    return persist();
}
void AppSettings::saveNavigation(int tab, QString query) {
    if (lastTab() == tab && lastQuery() == query)
        return;
    m_values["lastTab"] = qBound(0, tab, 4);
    m_values["lastQuery"] = query;
    m_dirty.unite({"lastTab", "lastQuery"});
    persist();
}
void AppSettings::saveWindow(int width, int height) {
    m_values["windowWidth"] = width;
    m_values["windowHeight"] = height;
    m_dirty.unite({"windowWidth", "windowHeight"});
    persist();
}
void AppSettings::rememberImport(const QUrl& file) {
    if (file.isLocalFile()) {
        m_values["importDirectory"] = QFileInfo(file.toLocalFile()).absolutePath();
        m_dirty.insert("importDirectory");
        persist();
    }
}
bool AppSettings::recordApplication(const QString& source,
                                    const QString& destination,
                                    const QString& backup,
                                    const QString& summary,
                                    const QString& appId) {
    auto records = m_values.value("history").toArray();
    records.prepend(QJsonObject{{"appId", appId},
                                {"source", source},
                                {"destination", destination},
                                {"backup", backup},
                                {"summary", summary},
                                {"date", QDateTime::currentDateTime().toString(Qt::ISODate)},
                                {"restored", false}});
    while (records.size() > 100)
        records.removeLast();
    m_values["history"] = records;
    m_pendingRecords.append(records.first());
    return persist();
}
bool AppSettings::markRestored(const QString& backup) {
    auto records = m_values.value("history").toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto record = records[i].toObject();
        if (record["backup"].toString() == backup) {
            record["restored"] = true;
            records[i] = record;
        }
    }
    m_values["history"] = records;
    m_pendingRestores.append(backup);
    return persist();
}
