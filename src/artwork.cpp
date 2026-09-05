#include "artwork.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>
#include <memory>
namespace {
bool validId(const QString& id) {
    return QRegularExpression("^[1-9][0-9]{0,9}$").match(id).hasMatch() &&
           id.toULongLong() <= 4294967295ULL;
}
bool validSize(const QSize& size) {
    return size.width() > 0 && size.height() > 0 && qint64(size.width()) * size.height() <= 8000000;
}
} // namespace
Artwork::Artwork(QString directory, QObject* parent, QUrl api, QUrl cdn)
    : QObject(parent), m_directory(directory + "/cache/covers"), m_api(api), m_cdn(cdn) {}
void Artwork::request(QString id) {
    if (!validId(id) || m_pending.contains(id))
        return;
    const auto file = m_directory + "/" + id + ".jpg";
    QImageReader reader(file);
    if (QFileInfo(file).size() <= 2 * 1024 * 1024 && validSize(reader.size()) &&
        !reader.read().isNull()) {
        m_images[id] = QUrl::fromLocalFile(file);
        m_states[id] = "ready";
        emit changed();
        if (QFileInfo(file).lastModified().daysTo(QDateTime::currentDateTime()) < 30)
            return;
    }
    if (m_failures.contains(id) && QDateTime::currentSecsSinceEpoch() - m_failures[id] < 60)
        return;
    m_pending.insert(id);
    m_queue.enqueue(id);
    m_states[id] = "loading";
    emit changed();
    pump();
}
void Artwork::pump() {
    while (m_active < 4 && !m_queue.isEmpty()) {
        ++m_active;
        resolve(m_queue.dequeue());
    }
}
void Artwork::get(const QUrl& url, qint64 limit, std::function<void(QByteArray)> done) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    auto reply = m_network.get(request);
    reply->setReadBufferSize(65536);
    auto bytes = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    auto consume = [reply, bytes, tooLarge, limit] {
        if (reply->isOpen())
            bytes->append(reply->readAll());
        if (bytes->size() > limit) {
            *tooLarge = true;
            reply->abort();
        }
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    auto timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(12000);
    connect(reply, &QNetworkReply::finished, this, [reply, bytes, tooLarge, consume, done] {
        consume();
        bool success = !*tooLarge && reply->error() == QNetworkReply::NoError &&
                       reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        done(success ? *bytes : QByteArray());
    });
}
void Artwork::resolve(const QString& id) {
    QJsonObject input{{"ids", QJsonArray{QJsonObject{{"appid", qint64(id.toULongLong())}}}},
                      {"context", QJsonObject{{"language", "english"}, {"country_code", "US"}}},
                      {"data_request", QJsonObject{{"include_assets", true}}}};
    auto url = m_api;
    QUrlQuery query;
    query.addQueryItem("input_json",
                       QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
    url.setQuery(query);
    get(url, 512 * 1024, [this, id](const QByteArray& data) {
        QString asset;
        auto items =
            QJsonDocument::fromJson(data).object()["response"].toObject()["store_items"].toArray();
        for (auto item : items) {
            auto object = item.toObject();
            if (object["appid"].toVariant().toString() == id) {
                asset = object["assets"].toObject()["header"].toString();
                break;
            }
        }
        // Relative header paths only — no hosts or `..` from the API.
        if (!QRegularExpression("^(?:[a-zA-Z0-9_-]+/)?header(?:_[a-zA-Z_]+)?\\.jpg$")
                 .match(asset)
                 .hasMatch())
            asset = "header.jpg";
        download(id, asset, asset == "header.jpg");
    });
}
void Artwork::download(const QString& id, const QString& asset, bool fallback) {
    auto url = m_cdn;
    url.setPath(url.path() + id + "/" + asset);
    get(url, 2 * 1024 * 1024, [this, id, fallback](QByteArray data) {
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        QImage image;
        if (validSize(reader.size()))
            image = reader.read();
        if (image.isNull()) {
            if (!fallback) {
                download(id, "header.jpg", true);
                return;
            }
            finish(id, false);
            return;
        }
        if (!QDir().mkpath(m_directory)) {
            finish(id, false);
            return;
        }
        const auto path = m_directory + "/" + id + ".jpg";
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            !image.scaled(460, 215, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                 .save(&file, "JPG", 88) ||
            !file.commit()) {
            finish(id, false);
            return;
        }
        m_images[id] = QUrl::fromLocalFile(path);
        trimCache(path);
        finish(id, true);
    });
}
void Artwork::finish(const QString& id, bool success) {
    m_pending.remove(id);
    --m_active;
    m_states[id] = (success || m_images.contains(id)) ? "ready" : "missing";
    if (!success)
        m_failures[id] = QDateTime::currentSecsSinceEpoch();
    else
        m_failures.remove(id);
    emit changed();
    pump();
}
void Artwork::trimCache(const QString& keep) {
    auto files =
        QDir(m_directory).entryInfoList({"*.jpg"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const auto& file : files)
        total += file.size();
    for (const auto& file : files) {
        if (total <= 128 * 1024 * 1024)
            break;
        if (file.absoluteFilePath() != keep && QFile::remove(file.absoluteFilePath()))
            total -= file.size();
    }
}
