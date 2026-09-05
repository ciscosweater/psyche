#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <functional>

// Steam covers go through a separate QNetworkAccessManager, not the Hubcap client.
class Artwork : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap images READ images NOTIFY changed)
    Q_PROPERTY(QVariantMap states READ states NOTIFY changed)
public:
    explicit Artwork(
        QString dataDirectory,
        QObject* parent = nullptr,
        QUrl api = QUrl("https://api.steampowered.com/IStoreBrowseService/GetItems/v1"),
        QUrl cdn = QUrl("https://shared.steamstatic.com/store_item_assets/steam/apps/"));
    QVariantMap images() const { return m_images; }
    QVariantMap states() const { return m_states; }
    Q_INVOKABLE void request(QString appId);
signals:
    void changed();

private:
    void pump();
    void resolve(const QString& id);
    void download(const QString& id, const QString& asset, bool fallback);
    void get(const QUrl& url, qint64 limit, std::function<void(QByteArray)> done);
    void finish(const QString& id, bool success);
    void trimCache(const QString& keep);
    QString m_directory;
    QUrl m_api, m_cdn;
    QNetworkAccessManager m_network;
    QVariantMap m_images, m_states;
    QQueue<QString> m_queue;
    QSet<QString> m_pending;
    QMap<QString, qint64> m_failures;
    int m_active = 0;
};
