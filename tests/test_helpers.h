#pragma once
#include "package.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <stdexcept>
static void checkAt(bool ok, const char* expression, int line) {
    if (!ok)
        throw std::runtime_error(std::string("Test failed at line ") + std::to_string(line) + ": " +
                                 expression);
}
#define PSYCHE_CHECK(...) checkAt((__VA_ARGS__), #__VA_ARGS__, __LINE__)
static void zip(QString path, QString name, QByteArray data) {
    auto a = archive_write_new();
    archive_write_set_format_zip(a);
    PSYCHE_CHECK(archive_write_open_filename(a, QFile::encodeName(path).constData()) == ARCHIVE_OK);
    auto e = archive_entry_new();
    archive_entry_set_pathname(e, name.toUtf8().constData());
    archive_entry_set_size(e, data.size());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    PSYCHE_CHECK(archive_write_header(a, e) == ARCHIVE_OK);
    PSYCHE_CHECK(archive_write_data(a, data.constData(), data.size()) == data.size());
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
}
