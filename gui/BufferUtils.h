#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

struct IntelHexResult {
    bool ok = false;
    QByteArray data;
    QString errorMessage;
};

namespace BufferUtils {

IntelHexResult parseIntelHex(const QByteArray &rawContent);
QByteArray toIntelHex(const QByteArray &data);
bool isLikelyIntelHex(const QString &filePath, const QByteArray &rawContent);

uint32_t computeCrc32(const QByteArray &data);
QString computeCrc32Hex(const QByteArray &data);
QString computeMd5Hex(const QByteArray &data);

// Parses a space/separator-tolerant hex string like "EF 40 17" or "EF4017"
bool parseHexPattern(const QString &text, QByteArray &outBytes);

} // namespace BufferUtils
