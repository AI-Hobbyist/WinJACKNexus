/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemePackage.hpp>

#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLibrary>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#include <zlib.h>

namespace WinJACKNexus::UI {
namespace {
constexpr quint64 maxPackageBytes = 32ULL * 1024 * 1024;
constexpr quint64 maxExpandedBytes = 128ULL * 1024 * 1024;
constexpr quint64 maxEntryBytes = 32ULL * 1024 * 1024;
constexpr quint64 maxAssetBytes = 16ULL * 1024 * 1024;
constexpr quint64 maxMetaBytes = 64ULL * 1024;
constexpr quint64 maxCssBytes = 1024ULL * 1024;
constexpr qsizetype maxEntries = 512;

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
bool has(const QByteArray& bytes, quint64 pos, quint64 size) {
    return pos <= quint64(bytes.size()) && size <= quint64(bytes.size()) - pos;
}
quint16 u16(const QByteArray& bytes, quint64 pos) {
    const auto* p = reinterpret_cast<const uchar*>(bytes.constData() + qsizetype(pos));
    return quint16(p[0]) | (quint16(p[1]) << 8);
}
quint32 u32(const QByteArray& bytes, quint64 pos) {
    return quint32(u16(bytes, pos)) | (quint32(u16(bytes, pos + 2)) << 16);
}
quint64 u64(const QByteArray& bytes, quint64 pos) {
    return quint64(u32(bytes, pos)) | (quint64(u32(bytes, pos + 4)) << 32);
}
void put16(QByteArray& bytes, quint16 value) {
    bytes.append(char(value & 0xff)); bytes.append(char((value >> 8) & 0xff));
}
void put32(QByteArray& bytes, quint32 value) {
    put16(bytes, quint16(value)); put16(bytes, quint16(value >> 16));
}
bool zip64Extra(const QByteArray& zip, quint64 begin, quint64 length,
                bool expandedNeeded, bool compressedNeeded, bool offsetNeeded,
                quint64* expanded, quint64* compressed, quint64* offset) {
    const auto end = begin + length;
    while (begin + 4 <= end) {
        const auto tag = u16(zip, begin);
        const auto size = u16(zip, begin + 2);
        begin += 4;
        if (begin + size > end) return false;
        if (tag == 1) {
            auto position = begin;
            const auto field = [&](bool needed, quint64* value) {
                if (!needed) return true;
                if (position + 8 > begin + size) return false;
                *value = u64(zip, position);
                position += 8;
                return true;
            };
            return field(expandedNeeded, expanded) && field(compressedNeeded, compressed)
                && field(offsetNeeded, offset);
        }
        begin += size;
    }
    return !expandedNeeded && !compressedNeeded && !offsetNeeded && begin == end;
}
bool utf8(const QByteArray& bytes, QString* text) {
    *text = QString::fromUtf8(bytes);
    return text->toUtf8() == bytes && !text->contains(QChar::Null);
}
bool validPath(const QString& path, bool directory) {
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':')) return false;
    if (path.endsWith('/') != directory) return false;
    const auto parts = path.split('/', Qt::KeepEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].isEmpty() && directory && i == parts.size() - 1) continue;
        if (parts[i].isEmpty() || parts[i] == "." || parts[i] == "..") return false;
        for (const auto character : parts[i]) if (character.unicode() < 0x20) return false;
    }
    return true;
}
bool allowedEntry(const QString& path, bool directory) {
    if (path == "meta.json" || path == "style.css") return !directory;
    if (path == "assets/") return directory;
    for (const auto& folder : {QStringLiteral("host/"), QStringLiteral("node/"), QStringLiteral("native_fx/")}) {
        if (path == folder) return directory;
        if (path.startsWith(folder) && !directory) {
            const auto file = path.mid(folder.size());
            static const QRegularExpression moduleName(QStringLiteral("^[a-z0-9]+(?:-[a-z0-9]+)*\\.css$"));
            return moduleName.match(file).hasMatch();
        }
    }
    if (!path.startsWith("assets/")) return false;
    if (directory) return true;
    const auto suffix = path.mid(path.lastIndexOf('.')).toLower();
    return suffix == ".png" || suffix == ".jpg" || suffix == ".jpeg"
        || suffix == ".webp" || suffix == ".ttf" || suffix == ".otf";
}
bool validMeta(const QByteArray& bytes, QJsonObject* meta, QString* error) {
    if (bytes.isEmpty() || quint64(bytes.size()) > maxMetaBytes)
        return fail(error, "meta.json size is invalid");
    QString decoded;
    if (!utf8(bytes, &decoded)) return fail(error, "meta.json is not UTF-8");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, "meta.json is not a JSON object");
    *meta = document.object();
    if (meta->value("formatVersion").toInt(-1) != 1 || meta->value("themeApiVersion").toInt(-1) != 1)
        return fail(error, "unsupported theme format/API version");
    const QRegularExpression idPattern(QStringLiteral("^[a-z0-9]+(?:\\.[a-z0-9]+)+-[a-z0-9]+(?:-[a-z0-9]+)*$"));
    const QRegularExpression versionPattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    const auto id = meta->value("id").toString();
    if (id.size() < 3 || id.size() > 128 || !idPattern.match(id).hasMatch())
        return fail(error, "invalid theme id");
    const auto version = meta->value("version").toString();
    if (!versionPattern.match(version).hasMatch()) return fail(error, "invalid theme version");
    const auto name = meta->value("name").toString();
    const auto author = meta->value("author").toString();
    const auto description = meta->value("description").toString();
    if (name.trimmed().isEmpty() || name.size() > 128 || author.trimmed().isEmpty()
        || author.size() > 128 || description.trimmed().isEmpty() || description.size() > 2048)
        return fail(error, "theme name/author/description is invalid");
    if (!meta->value("homepage").isString()) return fail(error, "theme homepage is missing");
    const auto homepage = meta->value("homepage").toString();
    if (!homepage.isEmpty()) {
        const QUrl url(homepage, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
            || url.host().isEmpty() || !url.userName().isEmpty() || !url.password().isEmpty()
            || homepage.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]"))))
            return fail(error, "invalid theme homepage");
    }
    const auto kind = meta->value("kind").toString();
    if (kind != "full" && kind != "fragment") return fail(error, "invalid theme kind");
    const auto namespaces = meta->value("namespaces");
    if (!namespaces.isArray() || namespaces.toArray().isEmpty()) return fail(error, "missing namespaces");
    QSet<QString> seen;
    const QRegularExpression scopePattern(QStringLiteral("^--wjn-theme-[a-z0-9]+(?:-[a-z0-9]+)*-$"));
    for (const auto& value : namespaces.toArray()) {
        if (!value.isString() || !scopePattern.match(value.toString()).hasMatch() || seen.contains(value.toString()))
            return fail(error, "invalid or duplicate namespace");
        seen.insert(value.toString());
    }
    if (meta->contains("preview")) {
        const auto preview = meta->value("preview");
        if (!preview.isString() || !validPath(preview.toString(), false)
            || !preview.toString().startsWith("assets/")
            || !QStringList {".png", ".jpg", ".jpeg", ".webp"}.contains(
                preview.toString().mid(preview.toString().lastIndexOf('.')).toLower()))
            return fail(error, "invalid preview path");
    }
    return true;
}

struct CentralEntry {
    QString name;
    quint64 localOffset = 0;
    quint64 compressed = 0;
    quint64 expanded = 0;
    quint32 crc = 0;
    quint16 method = 0;
    quint16 flags = 0;
    bool directory = false;
};
bool expand(const QByteArray& zip, quint64 pos, const CentralEntry& entry,
            QByteArray* output, QString* error) {
    if (entry.method == 0) {
        if (entry.compressed != entry.expanded) return fail(error, "stored entry size mismatch");
        *output = zip.mid(qsizetype(pos), qsizetype(entry.expanded));
    } else {
        output->clear();
        output->reserve(qsizetype(entry.expanded));
        z_stream stream {};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return fail(error, "zlib init failed");
        const auto* source = reinterpret_cast<const Bytef*>(zip.constData() + qsizetype(pos));
        quint64 consumed = 0;
        int status = Z_OK;
        char buffer[65536];
        while (status == Z_OK) {
            if (stream.avail_in == 0 && consumed < entry.compressed) {
                const auto chunk = uInt(std::min<quint64>(entry.compressed - consumed, 65536));
                stream.next_in = const_cast<Bytef*>(source + consumed);
                stream.avail_in = chunk;
                consumed += chunk;
            }
            stream.next_out = reinterpret_cast<Bytef*>(buffer);
            stream.avail_out = sizeof buffer;
            status = inflate(&stream, Z_NO_FLUSH);
            const auto produced = sizeof buffer - stream.avail_out;
            if (quint64(output->size()) + produced > entry.expanded) {
                inflateEnd(&stream);
                return fail(error, "expanded entry exceeded declared size");
            }
            output->append(buffer, qsizetype(produced));
            if (status == Z_OK && stream.avail_in == 0 && consumed == entry.compressed && produced == 0) break;
        }
        const bool valid = status == Z_STREAM_END && quint64(output->size()) == entry.expanded
            && stream.total_in == entry.compressed;
        inflateEnd(&stream);
        if (!valid) return fail(error, "deflate stream is truncated or malformed");
    }
    const auto actualCrc = quint32(crc32(0, reinterpret_cast<const Bytef*>(output->constData()), uInt(output->size())));
    if (actualCrc != entry.crc) return fail(error, "ZIP CRC mismatch: " + entry.name);
    return true;
}
bool parseZip(const QByteArray& zip, QMap<QString, QByteArray>* files, QString* error) {
    if (zip.size() < 22 || quint64(zip.size()) > maxPackageBytes)
        return fail(error, "package size is outside 22 bytes..32 MiB");
    qint64 eocd = -1;
    const auto begin = std::max<qint64>(0, zip.size() - 65557);
    for (qint64 pos = zip.size() - 22; pos >= begin; --pos) {
        if (u32(zip, quint64(pos)) == 0x06054b50 && has(zip, quint64(pos), 22)
            && quint64(pos) + 22 + u16(zip, quint64(pos) + 20) == quint64(zip.size())) {
            eocd = pos; break;
        }
    }
    if (eocd < 0) return fail(error, "ZIP end record missing");
    const auto end = quint64(eocd);
    quint64 count = u16(zip, end + 10);
    quint64 centralSize = u32(zip, end + 12);
    quint64 centralOffset = u32(zip, end + 16);
    quint64 directoryEnd = end;
    if (end >= 20 && u32(zip, end - 20) == 0x07064b50) {
        if (u32(zip, end - 16) != 0 || u32(zip, end - 4) != 1)
            return fail(error, "multi-disk ZIP64 is unsupported");
        directoryEnd = u64(zip, end - 12);
        if (!has(zip, directoryEnd, 56) || u32(zip, directoryEnd) != 0x06064b50
            || u64(zip, directoryEnd + 4) < 44
            || !has(zip, directoryEnd, 12 + u64(zip, directoryEnd + 4))
            || directoryEnd + 12 + u64(zip, directoryEnd + 4) != end - 20
            || u32(zip, directoryEnd + 16) != 0 || u32(zip, directoryEnd + 20) != 0
            || u64(zip, directoryEnd + 24) != u64(zip, directoryEnd + 32))
            return fail(error, "invalid ZIP64 end record");
        count = u64(zip, directoryEnd + 32);
        centralSize = u64(zip, directoryEnd + 40);
        centralOffset = u64(zip, directoryEnd + 48);
    }
    if (u16(zip, end + 4) != 0 || u16(zip, end + 6) != 0
        || (u16(zip, end + 8) != 0xffff && u16(zip, end + 8) != u16(zip, end + 10))
        || count > maxEntries || !has(zip, centralOffset, centralSize)
        || centralOffset + centralSize != directoryEnd)
        return fail(error, "unsupported ZIP disk or central directory bounds");
    quint64 cursor = centralOffset;
    quint64 declaredTotal = 0;
    std::vector<CentralEntry> entries;
    entries.reserve(count);
    QSet<QString> names;
    bool assetsDirectory = false;
    for (quint64 index = 0; index < count; ++index) {
        if (!has(zip, cursor, 46) || u32(zip, cursor) != 0x02014b50)
            return fail(error, "invalid ZIP central record");
        const auto madeBy = u16(zip, cursor + 4);
        const auto flags = u16(zip, cursor + 8);
        const auto method = u16(zip, cursor + 10);
        const auto crc = u32(zip, cursor + 16);
        quint64 compressed = u32(zip, cursor + 20);
        quint64 expanded = u32(zip, cursor + 24);
        const auto nameSize = u16(zip, cursor + 28);
        const auto extraSize = u16(zip, cursor + 30);
        const auto commentSize = u16(zip, cursor + 32);
        const auto external = u32(zip, cursor + 38);
        quint64 localOffset = u32(zip, cursor + 42);
        if (!has(zip, cursor + 46, quint64(nameSize) + extraSize + commentSize)
            || cursor + 46 + nameSize + extraSize + commentSize > directoryEnd)
            return fail(error, "truncated ZIP central entry");
        QString name;
        if (!utf8(zip.mid(qsizetype(cursor + 46), nameSize), &name))
            return fail(error, "entry name is not UTF-8");
        const bool directory = name.endsWith('/');
        if (!validPath(name, directory) || !allowedEntry(name, directory) || names.contains(name))
            return fail(error, "unsafe, unsupported, or duplicate ZIP path: " + name);
        if ((flags & ~(quint16(8) | quint16(0x800))) != 0
            || (method != 0 && method != 8) || u16(zip, cursor + 34) != 0)
            return fail(error, "unsupported ZIP encryption or method");
        const bool zip64Expanded = expanded == 0xffffffff;
        const bool zip64Compressed = compressed == 0xffffffff;
        const bool zip64Offset = localOffset == 0xffffffff;
        if ((zip64Expanded || zip64Compressed || zip64Offset)
            && !zip64Extra(zip, cursor + 46 + nameSize, extraSize,
                           zip64Expanded, zip64Compressed, zip64Offset,
                           &expanded, &compressed, &localOffset))
            return fail(error, "invalid ZIP64 central entry");
        if ((madeBy >> 8) == 3 && ((external >> 16) & 0xf000) == 0xa000)
            return fail(error, "ZIP symbolic links are not allowed");
        if (directory && expanded != 0) return fail(error, "directory has content");
        if (expanded > maxEntryBytes || declaredTotal + expanded > maxExpandedBytes
            || (!directory && name.startsWith("assets/") && expanded > maxAssetBytes)
            || (name == "meta.json" && expanded > maxMetaBytes)
            || (name.endsWith(".css") && expanded > maxCssBytes)
            || (expanded > 0 && (compressed == 0 || expanded > quint64(compressed) * 200)))
            return fail(error, "ZIP resource budget exceeded: " + name);
        declaredTotal += expanded;
        names.insert(name);
        if (name == "assets/") assetsDirectory = true;
        entries.push_back({name, localOffset, compressed, expanded, crc, method, flags, directory});
        cursor += 46 + nameSize + extraSize + commentSize;
    }
    if (cursor != directoryEnd || !names.contains("meta.json") || !names.contains("style.css") || !assetsDirectory)
        return fail(error, "ZIP root layout is incomplete");
    std::vector<std::pair<quint64, quint64>> occupied;
    quint64 actualTotal = 0;
    for (const auto& entry : entries) {
        const auto local = entry.localOffset;
        if (!has(zip, local, 30) || local >= centralOffset || u32(zip, local) != 0x04034b50)
            return fail(error, "invalid ZIP local header");
        if (u16(zip, local + 6) != entry.flags || u16(zip, local + 8) != entry.method)
            return fail(error, "ZIP local/central flags differ");
        const auto nameSize = u16(zip, local + 26);
        const auto extraSize = u16(zip, local + 28);
        const auto dataOffset = local + 30 + nameSize + extraSize;
        QString localName;
        if (!has(zip, local + 30, quint64(nameSize) + extraSize)
            || !utf8(zip.mid(qsizetype(local + 30), nameSize), &localName)
            || localName != entry.name || !has(zip, dataOffset, entry.compressed))
            return fail(error, "ZIP local name or data bounds differ");
        quint64 intervalEnd = dataOffset + entry.compressed;
        const auto localCompressed = u32(zip, local + 18);
        const auto localExpanded = u32(zip, local + 22);
        if ((entry.flags & 8) == 0) {
            quint64 checkedCompressed = localCompressed;
            quint64 checkedExpanded = localExpanded;
            const bool zip64Compressed = localCompressed == 0xffffffff;
            const bool zip64Expanded = localExpanded == 0xffffffff;
            quint64 unused = 0;
            if ((zip64Compressed || zip64Expanded)
                && !zip64Extra(zip, local + 30 + nameSize, extraSize,
                               zip64Expanded, zip64Compressed, false,
                               &checkedExpanded, &checkedCompressed, &unused))
                return fail(error, "invalid ZIP64 local entry");
            if (u32(zip, local + 14) != entry.crc || checkedCompressed != entry.compressed
                || checkedExpanded != entry.expanded)
                return fail(error, "ZIP local/central sizes differ");
        } else {
            const bool signature = has(zip, intervalEnd, 4) && u32(zip, intervalEnd) == 0x08074b50;
            if (signature) intervalEnd += 4;
            const bool zip64Descriptor = localCompressed == 0xffffffff || localExpanded == 0xffffffff;
            const auto descriptorSize = zip64Descriptor ? 20 : 12;
            if (!has(zip, intervalEnd, descriptorSize) || u32(zip, intervalEnd) != entry.crc
                || (zip64Descriptor ? u64(zip, intervalEnd + 4) : u32(zip, intervalEnd + 4)) != entry.compressed
                || (zip64Descriptor ? u64(zip, intervalEnd + 12) : u32(zip, intervalEnd + 8)) != entry.expanded)
                return fail(error, "invalid ZIP data descriptor");
            intervalEnd += descriptorSize;
        }
        if (intervalEnd > centralOffset) return fail(error, "ZIP entry overlaps central directory");
        occupied.emplace_back(local, intervalEnd);
        QByteArray value;
        if (!expand(zip, dataOffset, entry, &value, error)) return false;
        actualTotal += value.size();
        if (actualTotal > maxExpandedBytes) return fail(error, "expanded package budget exceeded");
        files->insert(entry.name, value);
    }
    std::sort(occupied.begin(), occupied.end());
    for (std::size_t i = 1; i < occupied.size(); ++i)
        if (occupied[i - 1].second > occupied[i].first) return fail(error, "ZIP entries overlap");
    return true;
}
bool writeZip(const QMap<QString, QByteArray>& files, QByteArray* zip, QString* error) {
    if (files.size() > maxEntries) return fail(error, "too many ZIP entries");
    struct Record { QString name; quint32 crc; quint32 size; quint32 offset; bool directory; };
    std::vector<Record> records;
    zip->clear();
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const auto name = it.key();
        const auto bytes = name.toUtf8();
        const auto& content = it.value();
        if (bytes.size() > std::numeric_limits<quint16>::max()
            || quint64(zip->size()) + 30 + bytes.size() + content.size() > maxPackageBytes)
            return fail(error, "ZIP output exceeds size budget");
        const auto crc = quint32(crc32(0, reinterpret_cast<const Bytef*>(content.constData()), uInt(content.size())));
        const auto offset = quint32(zip->size());
        put32(*zip, 0x04034b50); put16(*zip, 20); put16(*zip, 0x800); put16(*zip, 0);
        put16(*zip, 0); put16(*zip, 0); put32(*zip, crc);
        put32(*zip, quint32(content.size())); put32(*zip, quint32(content.size()));
        put16(*zip, quint16(bytes.size())); put16(*zip, 0);
        zip->append(bytes); zip->append(content);
        records.push_back({name, crc, quint32(content.size()), offset, name.endsWith('/')});
    }
    const auto centralOffset = quint32(zip->size());
    for (const auto& record : records) {
        const auto name = record.name.toUtf8();
        put32(*zip, 0x02014b50); put16(*zip, 20); put16(*zip, 20);
        put16(*zip, 0x800); put16(*zip, 0); put16(*zip, 0); put16(*zip, 0);
        put32(*zip, record.crc); put32(*zip, record.size); put32(*zip, record.size);
        put16(*zip, quint16(name.size())); put16(*zip, 0); put16(*zip, 0);
        put16(*zip, 0); put16(*zip, 0); put32(*zip, record.directory ? 0x10 : 0);
        put32(*zip, record.offset); zip->append(name);
    }
    const auto centralSize = quint32(zip->size()) - centralOffset;
    put32(*zip, 0x06054b50); put16(*zip, 0); put16(*zip, 0);
    put16(*zip, quint16(records.size())); put16(*zip, quint16(records.size()));
    put32(*zip, centralSize); put32(*zip, centralOffset); put16(*zip, 0);
    if (quint64(zip->size()) > maxPackageBytes) return fail(error, "ZIP output exceeds 32 MiB");
    return true;
}
} // namespace

struct ThemePackage::Data {
    QJsonObject metadata;
    QByteArray css;
    QMap<QString, QByteArray> entries;
};
ThemePackage::ThemePackage() = default;
ThemePackage::~ThemePackage() = default;
ThemePackage::ThemePackage(const ThemePackage&) = default;
ThemePackage& ThemePackage::operator=(const ThemePackage&) = default;
ThemePackage::ThemePackage(ThemePackage&&) noexcept = default;
ThemePackage& ThemePackage::operator=(ThemePackage&&) noexcept = default;
ThemePackage::ThemePackage(QSharedPointer<const Data> data) : data_(std::move(data)) {}

bool ThemePackage::loadFromFile(const QString& path, ThemePackage* result, QString* error) {
    if (!result) return fail(error, "null theme result");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "cannot open theme package: " + path);
    if (file.size() > qint64(maxPackageBytes)) return fail(error, "theme package exceeds 32 MiB");
    const auto bytes = file.read(maxPackageBytes + 1);
    file.close();
    return loadFromBytes(bytes, result, error);
}
bool ThemePackage::loadFromDirectory(const QString& path, ThemePackage* result, QString* error) {
    if (!result) return fail(error, "null theme result");
    const QFileInfo rootInfo(path);
    if (!rootInfo.isDir() || rootInfo.isSymLink()) return fail(error, "theme directory is missing or linked");
    const QDir root(rootInfo.absoluteFilePath());
    QMap<QString, QByteArray> entries;
    quint64 total = 0;
    QDirIterator iterator(root.absolutePath(),
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        const auto directory = info.isDir();
        auto logical = QDir::fromNativeSeparators(root.relativeFilePath(info.absoluteFilePath()));
        if (directory) logical += '/';
        if (!directory && QStringList {QStringLiteral("README.md"), QStringLiteral("AGENTS.md"),
                                       QStringLiteral("Theme-Customization.md"),
                                       QStringLiteral("build_theme.py"),
                                       QStringLiteral("winjacknexus-default.wjnskin"),
                                       QStringLiteral("assets/.gitkeep")}.contains(logical)) continue;
        if (info.isSymLink() || !validPath(logical, directory) || !allowedEntry(logical, directory))
            return fail(error, "unsupported Dev theme entry: " + logical);
        if (entries.contains(logical) || entries.size() >= maxEntries)
            return fail(error, "duplicate or excessive Dev theme entries");
        QByteArray content;
        if (!directory) {
            const auto limit = logical.startsWith("assets/") ? maxAssetBytes
                : logical == "meta.json" ? maxMetaBytes : maxCssBytes;
            if (info.size() < 0 || quint64(info.size()) > limit)
                return fail(error, "Dev theme entry exceeds size budget: " + logical);
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) return fail(error, "cannot read Dev theme entry: " + logical);
            content = file.readAll();
            if (content.size() != info.size()) return fail(error, "Dev theme entry changed while reading: " + logical);
            total += content.size();
            if (total > maxPackageBytes) return fail(error, "Dev theme exceeds package budget");
        }
        entries.insert(logical, content);
    }
    QByteArray zip;
    if (!writeZip(entries, &zip, error)) return false;
    return loadFromBytes(zip, result, error);
}
bool ThemePackage::loadDefaultFromLibrary(ThemePackage* result, QString* error) {
    if (!result) return fail(error, "null theme result");
    static QLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("wjn_default_theme.dll")));
    if (!library.load()) return fail(error, "required default theme DLL is unavailable: " + library.errorString());
    using Version = std::uint32_t (*)();
    using Data = const char* (*)(std::uint32_t*);
    const auto version = reinterpret_cast<Version>(library.resolve("wjn_default_theme_api_version"));
    const auto data = reinterpret_cast<Data>(library.resolve("wjn_default_theme_package_data"));
    if (!version || !data || version() != 1) return fail(error, "default theme DLL API is incompatible");
    std::uint32_t size = 0;
    const auto bytes = data(&size);
    if (!bytes || size == 0 || size > maxPackageBytes)
        return fail(error, "default theme DLL has no valid package data");
    return loadFromBytes(QByteArray(bytes, qsizetype(size)), result, error);
}
bool ThemePackage::loadFromBytes(const QByteArray& zip, ThemePackage* result, QString* error) {
    if (!result) return fail(error, "null theme result");
    QMap<QString, QByteArray> entries;
    if (!parseZip(zip, &entries, error)) return false;
    QJsonObject meta;
    if (!validMeta(entries.value("meta.json"), &meta, error)) return false;
    QString css;
    if (entries.value("style.css").isEmpty() || !utf8(entries.value("style.css"), &css))
        return fail(error, "style.css is empty or not UTF-8");
    QByteArray combined = entries.value("style.css");
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const auto& name = it.key();
        if (!name.endsWith(".css") || name == "style.css") continue;
        QString module;
        if (it.value().isEmpty() || !utf8(it.value(), &module))
            return fail(error, "module CSS is empty or not UTF-8: " + name);
        combined.append('\n');
        combined.append(it.value());
    }
    if (quint64(combined.size()) > maxCssBytes)
        return fail(error, "combined CSS exceeds 1 MiB");
    if (meta.contains("preview") && !entries.contains(meta.value("preview").toString()))
        return fail(error, "preview asset is missing");
    auto data = QSharedPointer<Data>::create();
    data->metadata = meta;
    data->css = std::move(combined);
    data->entries = std::move(entries);
    *result = ThemePackage(data);
    if (error) error->clear();
    return true;
}
bool ThemePackage::create(const QJsonObject& metadata, const QByteArray& styleCss,
                          const QMap<QString, QByteArray>& assets, ThemePackage* result, QString* error) {
    return create(metadata, styleCss, {}, assets, result, error);
}
bool ThemePackage::create(const QJsonObject& metadata, const QByteArray& styleCss,
                          const QMap<QString, QByteArray>& modules,
                          const QMap<QString, QByteArray>& assets, ThemePackage* result, QString* error) {
    if (!result) return fail(error, "null theme result");
    QMap<QString, QByteArray> entries = assets;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        if (!it.key().startsWith("assets/")) return fail(error, "asset map contains a non-asset ZIP name");
    for (auto it = modules.cbegin(); it != modules.cend(); ++it) {
        if (!allowedEntry(it.key(), false) || !it.key().endsWith(".css") || it.key() == "style.css")
            return fail(error, "invalid module CSS path: " + it.key());
        entries.insert(it.key(), it.value());
        entries.insert(it.key().left(it.key().indexOf('/') + 1), {});
    }
    entries.insert("assets/", {});
    entries.insert("meta.json", QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    entries.insert("style.css", styleCss);
    QByteArray zip;
    if (!writeZip(entries, &zip, error)) return false;
    return loadFromBytes(zip, result, error);
}
bool ThemePackage::withImportedAsset(const QString& sourcePath, const QString& logicalName,
                                     ThemePackage* result, QString* error) const {
    if (!data_ || !result) return fail(error, "invalid source theme or result");
    if (!validPath(logicalName, false) || !allowedEntry(logicalName, false))
        return fail(error, "invalid logical asset path");
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > qint64(maxAssetBytes))
        return fail(error, "cannot read imported asset or it exceeds 16 MiB");
    const auto bytes = file.read(maxAssetBytes + 1);
    file.close();
    auto entries = data_->entries;
    entries.insert(logicalName, bytes);
    QByteArray zip;
    if (!writeZip(entries, &zip, error)) return false;
    return loadFromBytes(zip, result, error);
}
bool ThemePackage::exportToBytes(QByteArray* zip, QString* error) const {
    if (!data_ || !zip) return fail(error, "invalid theme or output");
    return writeZip(data_->entries, zip, error);
}
bool ThemePackage::saveToFile(const QString& path, QString* error) const {
    QByteArray bytes;
    if (!exportToBytes(&bytes, error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "cannot write theme package: " + path);
    if (error) error->clear();
    return true;
}
bool ThemePackage::isValid() const noexcept { return bool(data_); }
bool ThemePackage::ResourceRef::isValid() const noexcept { return bool(bytes_); }
QString ThemePackage::ResourceRef::packageId() const { return packageId_; }
QString ThemePackage::ResourceRef::logicalName() const { return logicalName_; }
QByteArray ThemePackage::ResourceRef::bytes() const { return bytes_ ? *bytes_ : QByteArray {}; }
QJsonObject ThemePackage::metadata() const { return data_ ? data_->metadata : QJsonObject {}; }
QByteArray ThemePackage::styleCss() const { return data_ ? data_->css : QByteArray {}; }
QByteArray ThemePackage::resource(const QString& name) const {
    return data_ && name.startsWith("assets/") ? data_->entries.value(name) : QByteArray {};
}
ThemePackage::ResourceRef ThemePackage::resourceRef(const QString& name) const {
    ResourceRef result;
    if (data_ && name.startsWith("assets/") && data_->entries.contains(name) && !name.endsWith('/')) {
        result.packageId_ = id();
        result.logicalName_ = name;
        result.bytes_ = QSharedPointer<QByteArray>::create(data_->entries.value(name));
    }
    return result;
}
QStringList ThemePackage::resourceNames() const {
    QStringList result;
    if (data_) for (auto it = data_->entries.cbegin(); it != data_->entries.cend(); ++it)
        if (it.key().startsWith("assets/") && !it.key().endsWith('/')) result.append(it.key());
    return result;
}
QString ThemePackage::id() const { return data_ ? data_->metadata.value("id").toString() : QString {}; }
}
