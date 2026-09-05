#pragma once
#include "package.h"
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>
struct SearchPage {
    QVariantList games;
    bool hasMore = false;
};
class Catalog {
public:
    explicit Catalog(QString apiKey,
                     QUrl base = QUrl("https://hubcapmanifest.com/api/v1"),
                     QUrl appInfo = QUrl("https://api.steamcmd.net"));
    QVariantMap health() const;
    QVariantMap stats() const;
    SearchPage search(const QString& query, int offset = 0) const;
    Package fetch(const QString& appId, const QString& content = "full") const;
    static bool resolveGameName(Package& package,
                                QUrl base = QUrl("https://store.steampowered.com/api"));
    // Promote keyed IDs that steamcmd info says are apps. Fail closed: errors leave them as depots.
    static void promoteKeyedApps(Package& package, QUrl base = QUrl("https://api.steamcmd.net"));
    static QString contentLabel(const QString& content);
    static QString validateAppId(const QString& value);

private:
    QByteArray get(const QString& endpoint,
                   const QUrlQuery& query,
                   qint64 limit,
                   bool authenticated = true,
                   int timeout = 60000) const;
    QString m_key;
    QUrl m_base;
    QUrl m_appInfo;
};
