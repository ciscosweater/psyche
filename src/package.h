#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QString>
struct Package {
    QSet<QString> apps, depots;
    QMap<QString, QString> keys;
    QMap<QString, QString> labels;
    QJsonArray games;
    QString mainAppId;
    void setGameName(const QString& name);
    QString summary() const;
};
Package readLuaPackage(const QByteArray& data);
Package readPackage(const QString& path);
QString applyPackage(const Package& package, const QString& directory);
void restoreBackup(const QString& directory, const QString& backup);

QByteArray formatManagedYaml(QByteArray text);

void validateManagedYaml(const QByteArray& text);
