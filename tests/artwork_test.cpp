#include "artwork.h"
#include "test_helpers.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        PSYCHE_CHECK(temp.isValid());
        QTcpServer server;
        PSYCHE_CHECK(server.listen(QHostAddress::LocalHost));
        QList<QByteArray> requests;
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        PSYCHE_CHECK(buffer.open(QIODevice::WriteOnly));
        QImage image(460, 215, QImage::Format_RGB32);
        image.fill(QColor("#326da8"));
        PSYCHE_CHECK(image.save(&buffer, "JPG"));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto socket = server.nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, bytes] {
                *bytes += socket->readAll();
                if (!bytes->contains("\r\n\r\n"))
                    return;
                requests.append(*bytes);
                auto path = bytes->split(' ')[1];
                int status = 200;
                QByteArray body;
                if (path.startsWith("/api"))
                    body =
                        R"({"response":{"store_items":[{"appid":123,"assets":{"header":"abcdef/header.jpg"}},{"appid":456,"assets":{"header":"https://evil.invalid/header.jpg"}}]}})";
                else if (path == "/cdn/123/abcdef/header.jpg")
                    body = jpeg;
                else if (path == "/cdn/456/header.jpg")
                    body = jpeg;
                else {
                    status = 404;
                    body = "missing";
                }
                socket->write("HTTP/1.1 " + QByteArray::number(status) +
                              " Result\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
        Artwork artwork(temp.path(), nullptr, QUrl(base + "/api"), QUrl(base + "/cdn/"));
        auto wait = [&](QString id) {
            QElapsedTimer elapsed;
            elapsed.start();
            while (artwork.states()[id] == "loading" && elapsed.elapsed() < 3000) {
                QEventLoop loop;
                QTimer::singleShot(10, &loop, &QEventLoop::quit);
                loop.exec();
            }
            PSYCHE_CHECK(artwork.states()[id] != "loading");
        };
        artwork.request("123");
        artwork.request("123");
        wait("123");
        PSYCHE_CHECK(artwork.states()["123"] == "ready");
        PSYCHE_CHECK(requests.size() == 2);
        PSYCHE_CHECK(QFile::exists(temp.path() + "/cache/covers/123.jpg"));
        // Second Artwork instance should hit the cover cache, not HTTP.
        Artwork reopened(temp.path(), nullptr, QUrl(base + "/api"), QUrl(base + "/cdn/"));
        reopened.request("123");
        PSYCHE_CHECK(reopened.states()["123"] == "ready");
        PSYCHE_CHECK(requests.size() == 2);
        artwork.request("456");
        wait("456");
        PSYCHE_CHECK(artwork.states()["456"] == "ready");
        PSYCHE_CHECK(requests.last().startsWith("GET /cdn/456/header.jpg "));
        artwork.request("789");
        wait("789");
        PSYCHE_CHECK(artwork.states()["789"] == "missing");
        auto count = requests.size();
        artwork.request("789");
        artwork.request("../123");
        PSYCHE_CHECK(requests.size() == count);
        for (const auto& request : requests)
            PSYCHE_CHECK(!request.toLower().contains("authorization:"));
        std::cout << "Artwork: hashed URL, safe fallback, cache reuse, missing image and request "
                     "deduplication passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
