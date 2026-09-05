#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString destination READ destination NOTIFY changed)
    Q_PROPERTY(bool destinationValid READ destinationValid NOTIFY changed)
    Q_PROPERTY(QString steamDirectory READ steamDirectory NOTIFY changed)
    Q_PROPERTY(bool steamRunning READ steamRunning NOTIFY changed)
    Q_PROPERTY(QString pluginDirectory READ pluginDirectory NOTIFY changed)
    Q_PROPERTY(QString backupDirectory READ backupDirectory NOTIFY changed)
    Q_PROPERTY(QString libraryInject READ libraryInject NOTIFY changed)
    Q_PROPERTY(QString slsBinary READ slsBinary NOTIFY changed)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY changed)
    Q_PROPERTY(QVariantList steamDirectories READ steamDirectories NOTIFY changed)
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY changed)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)
    Q_PROPERTY(bool importedKey READ importedKey NOTIFY changed)
    Q_PROPERTY(bool environmentKey READ environmentKey NOTIFY changed)
    Q_PROPERTY(bool rememberKey READ rememberKey NOTIFY changed)
    Q_PROPERTY(QString downloadContent READ downloadContent NOTIFY changed)
    Q_PROPERTY(QString theme READ theme NOTIFY changed)
    Q_PROPERTY(QString lastQuery READ lastQuery NOTIFY changed)
    Q_PROPERTY(QUrl importDirectory READ importDirectory NOTIFY changed)
    Q_PROPERTY(int lastTab READ lastTab NOTIFY changed)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY changed)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool settingsError READ settingsError NOTIFY changed)
public:
    explicit AppSettings(QObject* parent = nullptr);
    // Test constructor: fake home/data roots instead of the real user dirs.
    AppSettings(QString home,
                QString data,
                QString config,
                QString xdgData,
                QObject* parent = nullptr,
                QString procRoot = QString(),
                QString systemLib = QString());
    QString dataDirectory() const { return m_data; }
    QString destination() const { return m_destination; }
    bool destinationValid() const;
    QString steamDirectory() const { return m_steam; }
    bool steamRunning() const { return m_steamRunning; }
    QString pluginDirectory() const;
    QString backupDirectory() const;
    QString libraryInject() const { return m_libraryInject; }
    QString slsBinary() const { return m_slsBinary; }
    QVariantList destinations() const { return m_destinations; }
    QVariantList steamDirectories() const { return m_steams; }
    QVariantList libraries() const { return m_libraries; }
    QString apiKey() const { return m_sessionKey; }
    QString effectiveApiKey() const;
    bool hasApiKey() const { return !effectiveApiKey().isEmpty(); }
    bool environmentKey() const;
    bool importedKey() const { return m_importedKey; }
    bool rememberKey() const { return m_values.value("rememberKey").toBool(false); }
    QString downloadContent() const;
    Q_INVOKABLE bool setDownloadContent(QString content);
    QString theme() const { return m_values.value("theme").toString("system"); }
    QString lastQuery() const { return m_values.value("lastQuery").toString(); }
    QUrl importDirectory() const;
    int lastTab() const { return qBound(0, m_values.value("lastTab").toInt(), 4); }
    int windowWidth() const { return qBound(620, m_values.value("windowWidth").toInt(1000), 2400); }
    int windowHeight() const {
        return qBound(580, m_values.value("windowHeight").toInt(800), 1600);
    }
    QVariantList history() const;
    QString message() const { return m_message; }
    bool settingsError() const { return m_error; }
    Q_INVOKABLE QUrl folderUrl(QString path) const {
        return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
    }
    Q_INVOKABLE void detectPaths();
    Q_INVOKABLE void refreshSteamRunning();
    Q_INVOKABLE bool chooseDestination(QUrl directory);
    Q_INVOKABLE bool chooseSteam(QUrl directory);
    Q_INVOKABLE bool useAutomaticPaths();
    Q_INVOKABLE bool savePreferences(QString key, bool remember, QString theme);
    Q_INVOKABLE void saveNavigation(int tab, QString query);
    Q_INVOKABLE void saveWindow(int width, int height);
    void rememberImport(const QUrl& file);
    bool recordApplication(const QString& source,
                           const QString& destination,
                           const QString& backup,
                           const QString& summary,
                           const QString& appId = QString());
    bool markRestored(const QString& backup);
signals:
    void changed();

private:
    void importAssellaKey();
    bool persist();
    bool reportError(const QString& message);
    QString m_home, m_data, m_config, m_xdgData, m_destination, m_steam, m_slsBinary,
        m_libraryInject, m_procRoot, m_systemLib, m_sessionKey, m_message;
    bool m_steamRunning = false;
    QJsonObject m_values;
    QSet<QString> m_dirty;
    QJsonArray m_pendingRecords;
    QStringList m_pendingRestores;
    QVariantList m_destinations, m_steams, m_libraries;
    bool m_importedKey = false;
    bool m_error = false, m_corrupt = false, m_unreadable = false;
};
