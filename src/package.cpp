#include "package.h"
#include "library.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
static void fail(const QString& s) {
    throw std::runtime_error(s.toStdString());
}
static void write(const QString& path, const QByteArray& data) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) ||
        !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        f.write(data) != data.size() || !f.commit())
        fail("Failed to write: " + path);
}
static QString id(const QString& s) {
    if (!QRegularExpression("^[1-9][0-9]{0,9}$").match(s).hasMatch() ||
        s.toULongLong() > 4294967295ULL)
        fail("Invalid ID: " + s);
    return s;
}
static void key(Package& p, QString depot, QString value) {
    id(depot);
    if (!QRegularExpression("^[a-fA-F0-9]{64}$").match(value).hasMatch())
        fail("Invalid key for depot " + depot);
    value = value.toLower();
    if (p.keys.contains(depot) && p.keys[depot] != value)
        fail("Conflicting keys in package");
    p.depots.insert(depot);
    p.keys[depot] = value;
}
// Tokenize Lua without running it. Strings and comments are not treated as calls.
struct LuaToken {
    QString text;
    bool string = false;
};
static void readLua(Package& p, const QString& source) {
    QList<LuaToken> tokens;
    const QRegularExpression longStart(R"(\[(=*)\[)");
    for (qsizetype i = 0; i < source.size();) {
        if (source[i].isSpace()) {
            ++i;
            continue;
        }
        bool comment = source.mid(i, 2) == "--";
        if (comment)
            i += 2;
        auto longMatch = longStart.match(source,
                                         i,
                                         QRegularExpression::NormalMatch,
                                         QRegularExpression::AnchorAtOffsetMatchOption);
        if (longMatch.hasMatch()) {
            QString close = "]" + longMatch.captured(1) + "]";
            auto end = source.indexOf(close, i + longMatch.capturedLength());
            if (end < 0)
                fail("Unterminated Lua block");
            if (!comment)
                tokens.append({QString(), true});
            i = end + close.size();
            continue;
        }
        if (comment) {
            auto end = source.indexOf('\n', i);
            i = end < 0 ? source.size() : end;
            continue;
        }
        if (source[i] == '\'' || source[i] == '"') {
            QChar quote = source[i++];
            QString value;
            bool closed = false;
            while (i < source.size()) {
                auto c = source[i++];
                if (c == quote) {
                    closed = true;
                    break;
                }
                if (c == '\\') {
                    if (i >= source.size())
                        break;
                    value += '\\';
                    value += source[i++];
                } else
                    value += c;
            }
            if (!closed)
                fail("Unterminated Lua string");
            tokens.append({value, true});
            continue;
        }
        auto begin = i++;
        if (source[begin].isLetterOrNumber() || source[begin] == '_')
            while (i < source.size() && (source[i].isLetterOrNumber() || source[i] == '_'))
                ++i;
        tokens.append({source.mid(begin, i - begin), false});
    }
    auto symbol = [&](qsizetype i, const QString& v) {
        return i < tokens.size() && !tokens[i].string && tokens[i].text == v;
    };
    const QRegularExpression digits("^[0-9]+$");
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        bool app = symbol(i, "addappid"), depot = symbol(i, "setdepotkey");
        if ((!app && !depot) ||
            (i > 0 && (symbol(i - 1, ".") || symbol(i - 1, ":") || symbol(i - 1, "function"))))
            continue;
        if (!symbol(i + 1, "(") || i + 2 >= tokens.size() || tokens[i + 2].string ||
            !digits.match(tokens[i + 2].text).hasMatch())
            continue;
        auto number = tokens[i + 2].text;
        if (app && symbol(i + 3, ")")) {
            p.apps.insert(id(number));
            if (p.mainAppId.isEmpty())
                p.mainAppId = id(number);
            i += 3;
            continue;
        }
        qsizetype k = i + 4;
        if (!symbol(i + 3, ","))
            continue;
        if (app) {
            if (k >= tokens.size() || tokens[k].string || !digits.match(tokens[k].text).hasMatch())
                continue;
            if (symbol(k + 1, ")")) {
                p.apps.insert(id(number));
                if (p.mainAppId.isEmpty())
                    p.mainAppId = id(number);
                i = k + 1;
                continue;
            }
            if (!symbol(k + 1, ","))
                continue;
            k += 2;
        }
        if (k < tokens.size() && tokens[k].string && symbol(k + 1, ")")) {
            key(p, number, tokens[k].text);
            i = k + 1;
        }
    }
}

// Reject symlink components, including ancestors, on destination and backup paths.
static void noSymlinks(const QString& path) {
    QString current = QDir::rootPath();
    for (const auto& part : QFileInfo(path).absoluteFilePath().split('/', Qt::SkipEmptyParts)) {
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink())
            fail("Symbolic link not allowed: " + current);
    }
}
void Package::setGameName(const QString& name) {
    if (name.trimmed().isEmpty())
        return;
    for (const auto& value : apps | depots)
        labels[value] = name;
    auto array = [](const QSet<QString>& values) {
        QJsonArray result;
        auto sorted = values.values();
        sorted.sort();
        for (const auto& v : sorted)
            result.append(v);
        return result;
    };
    QJsonObject keyObject;
    for (auto it = keys.begin(); it != keys.end(); ++it)
        keyObject[it.key()] = it.value();
    QString appId = mainAppId;
    for (const auto& value : apps)
        if (appId.isEmpty() || (mainAppId.isEmpty() && value.toULongLong() < appId.toULongLong()))
            appId = value;
    games = {QJsonObject{{"appId", appId},
                         {"name", name},
                         {"apps", array(apps)},
                         {"depots", array(depots)},
                         {"keys", keyObject}}};
}
QString Package::summary() const {
    QStringList a = apps.values(), d = depots.values();
    a.sort();
    d.sort();
    return QString("Apps: %1\nDepots: %2\nKeys: %3")
        .arg(a.isEmpty() ? "none" : a.join(", "), d.isEmpty() ? "none" : d.join(", "))
        .arg(keys.size());
}
Package readLuaPackage(const QByteArray& data) {
    if (data.size() > 8 * 1024 * 1024)
        fail("Lua exceeds the 8 MiB limit");
    Package package;
    readLua(package, QString::fromUtf8(data));
    if (package.apps.isEmpty() && package.depots.isEmpty())
        fail("No recognized configuration in Lua");
    return package;
}
Package readPackage(const QString& path) {
    Package p;
    std::unique_ptr<archive, decltype(&archive_read_free)> ar(archive_read_new(),
                                                              archive_read_free);
    archive_read_support_format_zip(ar.get());
    archive_read_support_filter_none(ar.get());
    if (archive_read_open_filename(ar.get(), QFile::encodeName(path).constData(), 16384) !=
        ARCHIVE_OK)
        fail("Could not open ZIP");
    archive_entry* entry;
    qint64 total = 0;
    int count = 0, result;
    while ((result = archive_read_next_header(ar.get(), &entry)) == ARCHIVE_OK) {
        if (++count > 1000)
            fail("Too many files in ZIP");
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (name.startsWith('/') || name.contains('\\') || name.split('/').contains("..") ||
            name.contains(':') || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            fail("Unsafe path in ZIP");
        if (archive_entry_filetype(entry) == AE_IFDIR)
            continue;
        if (archive_entry_filetype(entry) != AE_IFREG)
            fail("File type not allowed");
        QByteArray data;
        char buffer[16384];
        la_ssize_t n;
        while ((n = archive_read_data(ar.get(), buffer, sizeof(buffer))) > 0) {
            total += n;
            if (total > 32 * 1024 * 1024 || data.size() + n > 8 * 1024 * 1024)
                fail("ZIP exceeds size limit");
            data.append(buffer, n);
        }
        if (n < 0)
            fail("Damaged or encrypted ZIP");
        QString base = QFileInfo(name).fileName();
        if (base == "download.lua" || base == "spliced-tickets.lua") {
            continue; // Skip plugin files; they are installed separately.
        } else if (base.endsWith(".yaml") || base.endsWith(".yml")) {
            auto root = YAML::Load(data.toStdString());
            if (!root.IsMap())
                fail("YAML must be a mapping");
            for (auto field : {"AdditionalApps", "AdditionalDepots"})
                if (root[field]) {
                    if (!root[field].IsSequence())
                        fail("Invalid YAML list");
                    for (auto v : root[field])
                        (QString(field) == "AdditionalApps" ? p.apps : p.depots)
                            .insert(id(QString::fromStdString(v.as<std::string>())));
                }
            if (root["DecryptionKeys"]) {
                if (!root["DecryptionKeys"].IsMap())
                    fail("Invalid DecryptionKeys");
                for (auto v : root["DecryptionKeys"])
                    key(p,
                        QString::fromStdString(v.first.as<std::string>()),
                        QString::fromStdString(v.second.as<std::string>()));
            }
        } else if (base.endsWith(".lua")) {
            readLua(p, QString::fromUtf8(data));
        }
    }
    if (result != ARCHIVE_EOF)
        fail("Incomplete ZIP");
    if (p.apps.isEmpty() && p.depots.isEmpty())
        fail("No recognized configuration in ZIP");
    if (p.mainAppId.isEmpty() && p.apps.size() == 1)
        p.mainAppId = *p.apps.begin();
    p.setGameName(p.mainAppId.isEmpty() ? "Imported game" : "AppID " + p.mainAppId);
    return p;
}
void restoreBackupContents(const QString& directory, const QString& backup, bool recoverLegacy) {
    if (directory.trimmed().isEmpty() || backup.trimmed().isEmpty())
        fail("Destination and backup must be explicit");
    noSymlinks(directory);
    noSymlinks(backup);
    if (!QFileInfo(directory).isDir())
        fail("Destination unavailable");
    noSymlinks(backup + "/files.txt");
    QFile manifest(backup + "/files.txt");
    if (!manifest.open(QIODevice::ReadOnly))
        fail("Backup unavailable");
    struct RestoreFile {
        QString path;
        bool existed;
        QByteArray data;
    };
    QList<RestoreFile> files;
    QSet<QString> seen;
    for (auto line : QString::fromUtf8(manifest.readAll()).split('\n', Qt::SkipEmptyParts)) {
        QString relative = line.mid(2);
        if ((!line.startsWith("1 ") && !line.startsWith("0 ")) || seen.contains(relative) ||
            (relative != "config.yaml" && relative != ".psyche-library.json"))
            fail("Invalid backup");
        seen.insert(relative);
        QString dest = QDir(directory).filePath(relative);
        noSymlinks(dest);
        if (QFileInfo(dest).exists() && !QFileInfo(dest).isFile())
            fail("Destination is not a file");
        bool existed = line.startsWith("1 ");
        QByteArray data;
        if (existed) {
            noSymlinks(backup + "/" + relative);
            QFile f(backup + "/" + relative);
            if (!f.open(QIODevice::ReadOnly))
                fail("Incomplete backup");
            data = f.readAll();
        }
        files.append({dest, existed, data});
    }
    if (!seen.contains("config.yaml"))
        fail("Invalid backup");
    if (recoverLegacy && !seen.contains(".psyche-library.json")) {
        auto path = QDir(directory).filePath(".psyche-library.json");
        noSymlinks(path);
        if (QFileInfo(path).exists() && !QFileInfo(path).isFile())
            fail("Invalid library metadata destination");
        files.append({path, false, {}});
    }
    // Load the full backup before touching destination files.
    for (const auto& file : files) {
        noSymlinks(file.path);
        if (file.existed) {
            if (!QDir().mkpath(QFileInfo(file.path).absolutePath()))
                fail("Failed to restore directory");
            write(file.path, file.data);
        } else if (QFile::exists(file.path) && !QFile::remove(file.path))
            fail("Failed to restore file");
    }
}
static QByteArray fillEmptyField(QByteArray text,
                                 const QString& field,
                                 const QByteArray& replacement) {
    auto root = YAML::Load(text.toStdString());
    if (!root.IsMap() || !root[field.toStdString()].IsNull())
        return text;
    for (auto entry : root)
        if (entry.first.as<std::string>() == field.toStdString()) {
            int start = entry.first.Mark().pos, colon = text.indexOf(':', start);
            if (colon < 0)
                fail("Invalid empty YAML field");
            auto tail = QString::fromUtf8(text.mid(colon + 1));
            auto match =
                QRegularExpression("^([ \\t]*)(?:(?:null|Null|NULL|~)(?=[ \\t\\r\\n,#}]|$))?")
                    .match(tail);
            auto prefix = match.captured(1).toUtf8();
            if (prefix.isEmpty())
                prefix = " ";
            int length = match.captured().toUtf8().size();
            QByteArray suffix =
                (colon + 1 + length < text.size() && text[colon + 1 + length] == '#') ? " " : "";
            text.replace(colon + 1, length, prefix + replacement + suffix);
            break;
        }
    return text;
}

// Blank line before a newly appended option; leave existing spacing alone.
static QByteArray sectionGap(const QByteArray& text, int offset, const QByteArray& nl) {
    if (offset == 0)
        return {};
    const auto before = text.left(offset);
    if (before.endsWith(nl + nl))
        return {};
    return before.endsWith(nl) ? nl : nl + nl;
}

static QByteArray enablePlugins(QByteArray text, const QByteArray& nl) {
    text = fillEmptyField(text, "Plugins", "yes");
    auto root = YAML::Load(text.toStdString());
    if (root.IsMap() && root["Plugins"]) {
        auto node = root["Plugins"];
        if (!node.IsScalar())
            fail("Plugins must be a scalar value");
        int offset = node.Mark().pos;
        auto tail = QString::fromUtf8(text.mid(offset));
        auto match =
            QRegularExpression(R"token(^(?:'[^'\r\n]*'|"(?:[^"\\\r\n]|\\.)*"|[^\s#,\[\]{}]+))token")
                .match(tail);
        if (!match.hasMatch() || tail.startsWith('&') || tail.startsWith('*') ||
            tail.startsWith('!') || tail.startsWith('|') || tail.startsWith('>'))
            fail("Unsupported Plugins scalar format");
        auto token = match.captured().toUtf8();
        QByteArray replacement = "yes";
        if (token.startsWith('\''))
            replacement = "'yes'";
        else if (token.startsWith('"'))
            replacement = "\"yes\"";
        text.replace(offset, token.size(), replacement);
    } else if (root.IsMap() && root.Style() == YAML::EmitterStyle::Flow) {
        int offset = root.Mark().pos;
        if (offset < 0 || text[offset] != '{')
            fail("Unsupported root anchor");
        text.insert(offset + 1, "Plugins: yes, ");
    } else {
        int offset = text.size();
        auto decoded = QString::fromUtf8(text);
        auto end = QRegularExpression("(?m)^\\.\\.\\.(?:[ \\t]*(?:#.*)?)\\r?$").match(decoded);
        if (end.hasMatch())
            offset = decoded.left(end.capturedStart()).toUtf8().size();
        text.insert(offset, sectionGap(text, offset, nl) + "Plugins: yes" + nl);
    }
    auto checked = YAML::Load(text.toStdString());
    if (!checked.IsMap() || checked["Plugins"].as<std::string>() != "yes")
        fail("Failed to enable Plugins");
    return text;
}

// Rewrite empty or old psyche multiline flow collections in place.
static QByteArray blockCollection(QByteArray text, const QString& field) {
    auto root = YAML::Load(text.toStdString());
    if (!root.IsMap() || root.Style() == YAML::EmitterStyle::Flow)
        return text;
    auto node = root[field.toStdString()];
    if (!node || node.IsNull() || node.Style() != YAML::EmitterStyle::Flow)
        return text;
    const bool mapping = field == "DecryptionKeys";
    int start = node.Mark().pos, end = start + 1;
    bool comment = false, quoted = false;
    char quote = 0;
    if (start < 0 || text[start] != (mapping ? '{' : '['))
        return text;
    for (; end < text.size(); ++end) {
        char c = text[end];
        if (comment) {
            if (c == '\n')
                comment = false;
            continue;
        }
        if (quoted) {
            if (c == quote && text[end - 1] != '\\')
                quoted = false;
            continue;
        }
        if (c == '#') {
            comment = true;
            continue;
        }
        if (c == '\'' || c == '"') {
            quoted = true;
            quote = c;
            continue;
        }
        if (c == (mapping ? '}' : ']'))
            break;
    }
    if (end >= text.size())
        return text;
    auto interior = text.mid(start + 1, end - start - 1);
    if (node.size() == 0 && interior.trimmed().isEmpty()) {
        text.remove(start, end - start + 1);
        return text;
    }
    if (!interior.contains('\n'))
        return text; // Leave compact one-line collections as-is.
    QList<QPair<int, QByteArray>> rows;
    for (auto item : node) {
        auto value = mapping ? item.first : YAML::Node(item);
        int pos = value.Mark().pos;
        int lineStart = text.lastIndexOf('\n', pos - 1) + 1, lineEnd = text.indexOf('\n', pos);
        if (lineEnd < 0)
            return text;
        auto line = text.mid(lineStart, lineEnd - lineStart);
        auto pattern =
            mapping ? QString(R"(^([ \t]*)([0-9]+: ["'][a-fA-F0-9]{64}["']),?([ \t]*(?:#.*)?\r?)$)")
                    : QString(R"(^([ \t]*)([0-9]+),?([ \t]*(?:#.*)?\r?)$)");
        auto match = QRegularExpression(pattern).match(QString::fromUtf8(line));
        if (!match.hasMatch())
            return text;
        rows.append({lineStart,
                     match.captured(1).toUtf8() + (mapping ? QByteArray() : QByteArray("- ")) +
                         match.captured(2).toUtf8() + match.captured(3).toUtf8()});
    }
    int closingStart = text.lastIndexOf('\n', end - 1) + 1, closingEnd = text.indexOf('\n', end);
    if (closingEnd < 0)
        closingEnd = text.size();
    else
        ++closingEnd;
    if (!text.mid(closingStart, end - closingStart).trimmed().isEmpty())
        return text;
    if (text.mid(end + 1, closingEnd - end - 1).trimmed().isEmpty())
        text.remove(closingStart, closingEnd - closingStart);
    else
        text.remove(end, 1);
    std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a.first > b.first; });
    for (auto row : rows) {
        int lineEnd = text.indexOf('\n', row.first);
        text.replace(row.first, lineEnd - row.first, row.second);
    }
    text.remove(start, 1);
    return text;
}

// Touch only managed fields. Keep comments, other scalars, and newline style.
QByteArray formatManagedYaml(QByteArray text) {
    auto root = YAML::Load(text.toStdString());
    if (!root.IsMap() || root.Style() == YAML::EmitterStyle::Flow)
        return text;
    const QByteArray nl = text.contains("\r\n") ? "\r\n" : "\n";
    auto lines = text.split('\n');
    for (auto& line : lines)
        if (line.endsWith('\r'))
            line.chop(1);
    const QSet<QString> managed{"AdditionalApps", "AdditionalDepots", "DecryptionKeys", "Plugins"};
    struct Field {
        int line;
        bool managed;
        YAML::Node value;
        QString name;
    };
    QList<Field> fields;
    for (auto entry : root)
        fields.append({entry.first.Mark().line,
                       managed.contains(QString::fromStdString(entry.first.as<std::string>())),
                       entry.second,
                       QString::fromStdString(entry.first.as<std::string>())});
    QSet<int> remove;
    QSet<int> blankBefore;
    auto blank = [&](int line) {
        return line >= 0 && line < lines.size() && lines[line].trimmed().isEmpty();
    };
    for (int i = 0; i < fields.size(); ++i) {
        auto field = fields[i];
        int next = i + 1 < fields.size() ? fields[i + 1].line : lines.size();
        if (i > 0 && (field.managed || fields[i - 1].managed) &&
            (fields[i - 1].managed ||
             (fields[i - 1].value.IsScalar() && !QString::fromUtf8(lines[fields[i - 1].line])
                                                     .contains(QRegularExpression(":\\s*[|>]"))))) {
            int begin = field.line;
            while (begin > fields[i - 1].line + 1 && blank(begin - 1))
                --begin;
            // Leave a comment that belongs to the next option.
            if (begin > 0 && !lines[begin - 1].trimmed().startsWith('#')) {
                for (int j = begin; j < field.line; ++j)
                    remove.insert(j);
                blankBefore.insert(field.line);
            }
        }
        if (!field.managed)
            continue;
        while (lines[field.line].endsWith(' ') || lines[field.line].endsWith('\t'))
            lines[field.line].chop(1);
        auto node = field.value;
        if (node.IsSequence() || node.IsMap()) {
            int previous = field.line;
            for (auto item : node) {
                auto value = node.IsMap() ? item.first : YAML::Node(item);
                int row = value.Mark().line;
                if (row <= field.line || row >= next)
                    continue;
                bool onlyBlank = true;
                for (int j = previous + 1; j < row; ++j)
                    if (!blank(j))
                        onlyBlank = false;
                if (onlyBlank)
                    for (int j = previous + 1; j < row; ++j)
                        remove.insert(j);
                previous = row;
                if (field.name == "DecryptionKeys") {
                    auto match =
                        QRegularExpression(
                            R"(^([ \t]*[0-9]+:[ \t]*)(["'])([a-fA-F0-9]{64})\2([ \t]*(?:#.*)?)$)")
                            .match(QString::fromUtf8(lines[row]));
                    if (match.hasMatch())
                        lines[row] =
                            (match.captured(1) + match.captured(3) + match.captured(4)).toUtf8();
                }
            }
        }
        if (i + 1 == fields.size()) {
            for (int marker = field.line + 1; marker < lines.size(); ++marker)
                if (QRegularExpression("^\\.\\.\\.(?:[ \\t]*(?:#.*)?)$")
                        .match(QString::fromUtf8(lines[marker]))
                        .hasMatch()) {
                    int begin = marker;
                    while (begin > field.line + 1 && blank(begin - 1))
                        --begin;
                    for (int j = begin; j < marker; ++j)
                        remove.insert(j);
                    blankBefore.insert(marker);
                }
            int end = lines.size();
            while (end > field.line + 1 && blank(end - 1))
                remove.insert(--end);
        }
    }
    QByteArray result;
    for (int i = 0; i < lines.size(); ++i) {
        if (remove.contains(i))
            continue;
        if (blankBefore.contains(i))
            result += nl;
        result += lines[i];
        if (i + 1 < lines.size())
            result += nl;
    }
    if (!fields.isEmpty() && fields.last().managed && !result.endsWith(nl))
        result += nl;
    // Parse after edits; don't round-trip through yaml-cpp emit.
    YAML::Load(result.toStdString());
    return result;
}

// Reject anchors/aliases on managed fields so edits stay in that collection.
void validateManagedYaml(const QByteArray& text) {
    auto documents = YAML::LoadAll(text.toStdString());
    if (documents.size() > 1)
        fail("Use only one YAML document in config.yaml");
    auto root = YAML::Load(text.toStdString());
    if (!root.IsMap())
        return;
    const QSet<QString> managed{"AdditionalApps", "AdditionalDepots", "DecryptionKeys", "Plugins"};
    for (auto entry : root) {
        auto field = QString::fromStdString(entry.first.as<std::string>());
        if (!managed.contains(field))
            continue;
        int start = entry.first.Mark().pos, colon = text.indexOf(':', start);
        if (colon < 0)
            fail("Invalid managed YAML field");
        auto tail = text.mid(colon + 1).trimmed();
        if (tail.startsWith('&') || tail.startsWith('*') || tail.startsWith('!'))
            fail("Anchors, aliases and tags are not supported in " + field);
        if (!entry.second.IsNull() && entry.second.Mark().pos < colon)
            fail("Aliased configuration is not supported in " + field);
        auto checkNode = [&](const YAML::Node& node) {
            if (node.IsNull())
                return;
            int pos = node.Mark().pos;
            if (pos < colon || (pos >= 0 && pos < text.size() &&
                                (text[pos] == '&' || text[pos] == '*' || text[pos] == '!')))
                fail("Anchors, aliases and tags are not supported in " + field);
        };
        checkNode(entry.second);
        if (entry.second.IsSequence())
            for (auto child : entry.second)
                checkNode(child);
        if (entry.second.IsMap())
            for (auto child : entry.second) {
                checkNode(child.first);
                checkNode(child.second);
            }
    }
}

// Patch the original text. yaml-cpp only validates and finds node offsets.
static QByteArray mergeYaml(QByteArray text, const Package& p) {
    validateManagedYaml(text);
    if (YAML::LoadAll(text.toStdString()).size() > 1)
        fail("Use only one YAML document in config.yaml");
    const QByteArray nl = text.contains("\r\n") ? "\r\n" : "\n";
    for (const QString field : {"AdditionalApps", "AdditionalDepots", "DecryptionKeys"}) {
        text = blockCollection(text, field);
        auto before = YAML::Load(text.toStdString());
        if (before.IsMap() && before.Style() == YAML::EmitterStyle::Flow)
            text = fillEmptyField(text, field, field == "DecryptionKeys" ? "{}" : "[]");
        auto root = YAML::Load(text.toStdString());
        if (root && !root.IsNull() && !root.IsMap())
            fail("Invalid existing configuration");
        QSet<QString> fields;
        if (root.IsMap())
            for (auto entry : root) {
                auto k = QString::fromStdString(entry.first.as<std::string>());
                if (fields.contains(k))
                    fail("Duplicate YAML field: " + k);
                fields.insert(k);
            }
        auto node = root.IsMap() ? root[field.toStdString()] : YAML::Node();
        bool mapping = field == "DecryptionKeys";
        bool exists = fields.contains(field);
        if (exists && !node.IsNull() && (mapping ? !node.IsMap() : !node.IsSequence()))
            fail("Invalid format in " + field);
        QSet<QString> existing;
        if (exists) {
            if (mapping)
                for (auto item : node)
                    existing.insert(QString::fromStdString(item.first.as<std::string>()));
            else
                for (auto item : node)
                    existing.insert(QString::fromStdString(item.as<std::string>()));
        }
        auto values =
            mapping ? p.keys.keys() : (field == "AdditionalApps" ? p.apps : p.depots).values();
        values.sort();
        QStringList additions;
        for (const auto& value : values) {
            if (mapping && existing.contains(value) &&
                QString::fromStdString(node[value.toStdString()].as<std::string>()).toLower() !=
                    p.keys[value])
                fail("Key conflicts with configuration: " + value);
            if (!existing.contains(value))
                additions << value;
        }
        if (additions.isEmpty())
            continue;
        bool rootFlow = !exists && root.IsMap() && root.Style() == YAML::EmitterStyle::Flow;
        bool flow = rootFlow || (exists && node.Style() == YAML::EmitterStyle::Flow);
        int offset = text.size(), indent = 2;
        if (exists && node.IsNull()) {
            text = fillEmptyField(text, field, "");
            auto fresh = YAML::Load(text.toStdString());
            for (auto entry : fresh)
                if (entry.first.as<std::string>() == field.toStdString()) {
                    offset = text.indexOf('\n', entry.first.Mark().pos);
                    if (offset < 0) {
                        text += nl;
                        offset = text.size();
                    } else
                        ++offset;
                    break;
                }
        } else if (exists) {
            offset = node.Mark().pos;
            if (flow) {
                if (offset < 0 || offset >= text.size() || text[offset] != (mapping ? '{' : '['))
                    fail("Unsupported collection anchor/alias in " + field);
                ++offset;
            } else {
                indent = node.Mark().column;
                int lineStart = text.lastIndexOf('\n', offset - 1) + 1;
                if (text.mid(lineStart, offset - lineStart).trimmed().size())
                    fail("Unsupported YAML collection in " + field);
                offset = lineStart;
            }
        } else {
            if (rootFlow) {
                offset = root.Mark().pos;
                if (offset < 0 || text[offset] != '{')
                    fail("Unsupported root anchor");
                ++offset;
            }
            // Insert before a trailing `...` document-end marker.
            auto end = QRegularExpression("(?m)^\\.\\.\\.(?:[ \\t]*(?:#.*)?)\\r?$")
                           .match(QString::fromUtf8(text));
            if (!rootFlow && end.hasMatch())
                offset = QString::fromUtf8(text).left(end.capturedStart()).toUtf8().size();
        }
        QByteArray inserted;
        if (flow)
            inserted += nl;
        if (rootFlow)
            inserted += field.toUtf8() + ": " + (mapping ? "{" : "[") + nl;
        else if (!exists) {
            inserted += sectionGap(text, offset, nl);
            inserted += field.toUtf8() + ":" + nl;
        }
        for (const auto& value : additions) {
            QString name = p.labels.value(value, "AppID " + value).simplified();
            name.replace(QRegularExpression("[\\x00-\\x1f\\x7f]"), " ");
            QByteArray entry =
                mapping ? value.toUtf8() + ": " + p.keys[value].toUtf8() : value.toUtf8();
            inserted += QByteArray(flow ? 2 : indent, ' ') +
                        (mapping || flow ? QByteArray() : QByteArray("- ")) + entry +
                        (flow ? "," : "") + " # " + name.toUtf8() + nl;
        }
        if (rootFlow)
            inserted += (mapping ? "}" : "]") + QByteArray(",") + nl;
        else if (!exists && offset < text.size())
            inserted += nl;
        text.insert(offset, inserted);
        auto checked = YAML::Load(text.toStdString());
        if (!checked.IsMap())
            fail("Failed to validate YAML changes");
    }
    return formatManagedYaml(enablePlugins(text, nl));
}

QString applyConfiguration(const Package& p, const QString& directory) {
    if (directory.trimmed().isEmpty())
        fail("Select an explicit destination directory");
    noSymlinks(directory);
    noSymlinks(QDir(directory).filePath("backups"));
    QDir dir(directory);
    if (!dir.exists() || QFileInfo(directory).isSymLink())
        fail("Select an existing SLSsteam directory");
    if (QFileInfo(dir.filePath("backups")).isSymLink())
        fail("Destination directories cannot be symbolic links");
    QStringList files{"config.yaml"};
    for (auto f : files)
        if (QFileInfo(dir.filePath(f)).isSymLink())
            fail("Destination file is a symbolic link");
    QByteArray original;
    QFile config(dir.filePath("config.yaml"));
    if (config.exists()) {
        if (!config.open(QIODevice::ReadOnly))
            fail("Could not read config.yaml");
        original = config.readAll();
    }
    auto updated = mergeYaml(original, p);
    QString backup =
        dir.filePath("backups/psyche-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(backup))
        fail("Failed to create backup");
    QByteArray manifest;
    for (auto f : files) {
        QFile original(dir.filePath(f));
        bool exists = original.exists();
        manifest += (exists ? "1 " : "0 ") + f.toUtf8() + "\n";
        if (exists) {
            if (!original.open(QIODevice::ReadOnly))
                fail("Failed to read file for backup");
            write(backup + "/" + f, original.readAll());
        }
    }
    write(backup + "/files.txt", manifest);
    try {
        write(dir.filePath("config.yaml"), updated);
    } catch (...) {
        restoreBackupContents(directory, backup);
        throw;
    }
    return backup;
}
