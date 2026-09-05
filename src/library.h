#pragma once
#include "package.h"
#include <QVariantList>
QVariantList installedGames(const QString& directory);
QString removeInstalledGame(const QString& directory, const QString& gameId);
// Writer used by applyPackage; not a public import entry point.
QString applyConfiguration(const Package& package, const QString& directory);

void restoreBackupContents(const QString& directory,
                           const QString& backup,
                           bool recoverLegacy = false);
