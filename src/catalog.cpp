#include "catalog.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <stdexcept>
namespace {
void error(const QString& text) {
    throw std::runtime_error(text.toStdString());
}
QString field(const QJsonObject& object, const QStringList& names) {
    for (const auto& name : names) {
        auto value = object.value(name);
        auto text = value.isString()   ? value.toString().trimmed()
                    : value.isDouble() ? QString::number(value.toDouble(), 'f', 0)
                                       : QString();
        if (!text.isEmpty())
            return text;
    }
    return {};
}
} // namespace
Catalog::Catalog(QString apiKey, QUrl base, QUrl appInfo)
    : m_key(apiKey.trimmed()), m_base(base), m_appInfo(appInfo) {}
QString Catalog::validateAppId(const QString& value) {
    if (!QRegularExpression("^[1-9][0-9]{0,9}$").match(value).hasMatch() ||
        value.toULongLong() > 4294967295ULL)
        error("Invalid AppID: " + value);
    return value;
}
QByteArray Catalog::get(const QString& endpoint,
                        const QUrlQuery& query,
                        qint64 limit,
                        bool authenticated,
                        int timeout) const {
    if (authenticated && m_key.isEmpty())
        error("Set your Hubcap key in the app or PSYCHE_HUBCAP_API_KEY.");
    if (authenticated && (m_key.contains('\r') || m_key.contains('\n')))
        error("Invalid Hubcap key.");
    auto url = m_base;
    url.setPath(url.path() + endpoint);
    url.setQuery(query);
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    if (authenticated)
        request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setRawHeader("User-Agent", "psyche/" PSYCHE_VERSION);
    // Don't follow redirects with the Hubcap bearer token. TLS stays on.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(qMin(15000, timeout));
    auto reply = manager.get(request);
    reply->setReadBufferSize(64 * 1024);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray data;
    bool tooLarge = false, timedOut = false;
    auto read = [&] {
        data += reply->readAll();
        if (data.size() > limit) {
            tooLarge = true;
            reply->abort();
        }
    };
    QObject::connect(reply, &QIODevice::readyRead, &loop, read);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        reply->abort();
    });
    timer.start(timeout);
    loop.exec();
    read();
    timer.stop();
    if (tooLarge)
        error("Hubcap response exceeds size limit.");
    if (timedOut)
        error("Hubcap request timed out.");
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401)
        error("Missing, invalid, or expired Hubcap key (401). Renew it if it expired.");
    if (status == 403)
        error("Hubcap access denied (403).");
    if (status == 404)
        error("AppID/package not found on Hubcap (404).");
    if (status == 429)
        error("Hubcap rate limit reached (429).");
    if (status == 503 && endpoint == "/health")
        return data;
    if (status >= 300)
        error(QString("Hubcap API error (HTTP %1).").arg(status));
    if (reply->error() != QNetworkReply::NoError)
        error("Hubcap connection error: " + reply->errorString());
    if (status != 200)
        error("Unexpected Hubcap response.");
    return data;
}
namespace {
QVariantMap responseObject(const QByteArray& bytes) {
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        error("Invalid Hubcap status response.");
    return doc.object().toVariantMap();
}
} // namespace
QVariantMap Catalog::health() const {
    auto result = responseObject(get("/health", {}, 256 * 1024, false));
    auto status = result.value("status").toString();
    if (status != "healthy" && status != "degraded")
        error("Unknown Hubcap health status.");
    return {{"status", status}};
}
QVariantMap Catalog::stats() const {
    auto result = responseObject(get("/user/stats", {}, 256 * 1024));
    if (result.contains("error"))
        error("Hubcap could not return account statistics.");
    QVariantMap safe;
    for (const auto& field : {"username",
                              "daily_usage",
                              "daily_limit",
                              "api_key_usage_count",
                              "api_key_expires_at",
                              "can_make_requests"})
        if (result.contains(field))
            safe[field] = result[field];
    if (safe.isEmpty())
        error("Missing Hubcap account statistics.");
    return safe;
}
SearchPage Catalog::search(const QString& query, int offset) const {
    if (query.trimmed().isEmpty())
        error("Enter a game name.");
    if (offset < 0)
        error("Invalid offset.");
    QUrlQuery params;
    params.addQueryItem("search", query.trimmed());
    params.addQueryItem("limit", "100");
    params.addQueryItem("offset", QString::number(offset));
    params.addQueryItem("sort_by", "name");
    auto bytes = get("/library", params, 4 * 1024 * 1024);
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        error("Invalid JSON response from Hubcap.");
    QJsonArray rows;
    auto root = doc.object();
    if (doc.isArray())
        rows = doc.array();
    else if (root.value("games").isArray())
        rows = root.value("games").toArray();
    else if (root.value("results").isArray())
        rows = root.value("results").toArray();
    else
        error("Invalid Hubcap search format.");
    SearchPage result;
    QSet<QString> seen;
    for (auto row : rows) {
        if (!row.isObject())
            error("Invalid Hubcap result.");
        auto object = row.toObject();
        auto appId = field(object, {"game_id", "app_id", "appid", "appId", "id"});
        try {
            validateAppId(appId);
        } catch (const std::exception&) {
            continue;
        }
        if (seen.contains(appId))
            continue;
        seen.insert(appId);
        auto name = field(object, {"name", "game_name", "title"});
        result.games.append(
            QVariantMap{{"appId", appId}, {"name", name.isEmpty() ? "Unnamed" : name}});
    }
    auto total = root.value("total_count");
    bool valid = false;
    auto count = total.toVariant().toLongLong(&valid);
    result.hasMore = !rows.isEmpty() && (valid ? offset + rows.size() < count : rows.size() >= 100);
    return result;
}
QString Catalog::contentLabel(const QString& content) {
    if (content == "full")
        return "Full Lua";
    if (content == "basegame")
        return "Lua • base game";
    if (content == "dlc")
        return "Lua • DLCs";
    if (content == "zip")
        return "Full ZIP";
    error("Invalid content. Use full, basegame, dlc or zip.");
    return {};
}
Package Catalog::fetch(const QString& appId, const QString& content) const {
    contentLabel(content);
    validateAppId(appId);
    Package package;
    if (content != "zip") {
        auto endpoint = content == "full" ? "/lua/" + appId : "/lua/" + content + "/" + appId;
        package = readLuaPackage(get(endpoint, {}, 8 * 1024 * 1024));
    } else {
        auto bytes = get("/manifest/" + appId, {}, 32 * 1024 * 1024);
        QTemporaryFile zip;
        if (!zip.open() || zip.write(bytes) != bytes.size() || !zip.flush())
            error("Failed to prepare temporary ZIP.");
        package = readPackage(zip.fileName());
    }
    package.mainAppId = appId;
    promoteKeyedApps(package, m_appInfo);
    package.setGameName("AppID " + appId);
    return package;
}

void Catalog::promoteKeyedApps(Package& package, QUrl base) {
    if (!base.isValid() || base.isEmpty())
        return;
    auto pending = (package.depots - package.apps).values();
    if (pending.isEmpty())
        return;
    pending.sort();
    QSet<QString> confirmed;
    for (const auto& id : pending) {
        try {
            auto bytes = Catalog({}, base).get("/v1/info/" + id, {}, 1024 * 1024, false, 8000);
            QJsonParseError parse;
            auto doc = QJsonDocument::fromJson(bytes, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject())
                continue;
            auto root = doc.object();
            auto status = root["status"].toString();
            if (!status.isEmpty() && status != "success")
                continue;
            auto info = root["data"].toObject().value(id);
            if (!info.isObject())
                continue;
            auto common = info.toObject()["common"];
            // Apps return PICS `common`; depots come back as {}.
            if (!common.isObject() || common.toObject().isEmpty())
                continue;
            confirmed.insert(id);
        } catch (const std::exception&) {
            continue;
        }
    }
    if (confirmed.isEmpty())
        return;
    package.apps.unite(confirmed);
    auto name = !package.games.isEmpty()
                    ? package.games.first().toObject()["name"].toString()
                    : (!package.mainAppId.isEmpty() ? "AppID " + package.mainAppId : QString());
    if (!name.isEmpty())
        package.setGameName(name);
}

// Steam store lookup uses an unauthenticated Catalog; no Hubcap token.
bool Catalog::resolveGameName(Package& package, QUrl base) {
    auto appId = package.mainAppId;
    if (appId.isEmpty() && package.apps.size() == 1)
        appId = *package.apps.begin();
    if (appId.isEmpty())
        return false;
    try {
        validateAppId(appId);
        QUrlQuery query;
        query.addQueryItem("appids", appId);
        query.addQueryItem("l", "english");
        query.addQueryItem("filters", "basic");
        auto bytes = Catalog({}, base).get("/appdetails", query, 1024 * 1024, false, 8000);
        auto result = QJsonDocument::fromJson(bytes).object()[appId].toObject();
        auto data = result["data"].toObject();
        auto name = data["name"].toString().trimmed();
        if (!result["success"].toBool() || data["steam_appid"].toVariant().toString() != appId ||
            name.isEmpty() || name.size() > 512)
            return false;
        name.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
        package.mainAppId = appId;
        package.setGameName(name);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
