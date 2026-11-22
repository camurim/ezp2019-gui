#include "BufferUtils.h"

#include <QCryptographicHash>
#include <QList>

#include <algorithm>
#include <array>
#include <vector>

namespace BufferUtils {

namespace {

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parseHexByte(const char *p, uint8_t &out)
{
    const int hi = hexDigit(p[0]);
    const int lo = hexDigit(p[1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<uint8_t>((hi << 4) | lo);
    return true;
}

const std::array<uint32_t, 256> &crc32Table()
{
    static const std::array<uint32_t, 256> table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

bool isLikelyIntelHex(const QString &filePath, const QByteArray &rawContent)
{
    if (filePath.endsWith(QStringLiteral(".hex"), Qt::CaseInsensitive) ||
        filePath.endsWith(QStringLiteral(".ihx"), Qt::CaseInsensitive) ||
        filePath.endsWith(QStringLiteral(".ihex"), Qt::CaseInsensitive)) {
        return true;
    }

    // Heuristic: starts with ':' followed by hex chars
    qsizetype i = 0;
    while (i < rawContent.size() && (rawContent[i] == ' ' || rawContent[i] == '\t' ||
                                     rawContent[i] == '\r' || rawContent[i] == '\n')) {
        ++i;
    }
    if (i + 11 <= rawContent.size() && rawContent[i] == ':') {
        for (qsizetype k = 1; k <= 10; ++k) {
            if (hexDigit(rawContent[i + k]) < 0) {
                return false;
            }
        }
        return true;
    }
    return false;
}

IntelHexResult parseIntelHex(const QByteArray &rawContent)
{
    struct Chunk {
        uint64_t address;
        QByteArray bytes;
    };

    std::vector<Chunk> chunks;
    uint64_t baseAddr = 0;
    uint64_t maxEnd = 0;
    bool seenEof = false;

    const QList<QByteArray> lines = rawContent.split('\n');
    int lineNum = 0;

    for (QByteArray line : lines) {
        ++lineNum;
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line[0] != ':') {
            return {false, {}, QStringLiteral("Linha %1: formato Intel HEX inválido (esperado ':').").arg(lineNum)};
        }
        if (line.size() < 11 || ((line.size() - 1) % 2) != 0) {
            return {false, {}, QStringLiteral("Linha %1: comprimento de registro inválido.").arg(lineNum)};
        }

        uint8_t byteCount = 0;
        uint8_t addrHi = 0;
        uint8_t addrLo = 0;
        uint8_t recType = 0;
        if (!parseHexByte(line.constData() + 1, byteCount) ||
            !parseHexByte(line.constData() + 3, addrHi) ||
            !parseHexByte(line.constData() + 5, addrLo) ||
            !parseHexByte(line.constData() + 7, recType)) {
            return {false, {}, QStringLiteral("Linha %1: caracteres hexadecimais inválidos.").arg(lineNum)};
        }

        const int expectedLen = 1 + (4 + byteCount + 1) * 2;
        if (line.size() != expectedLen) {
            return {false, {}, QStringLiteral("Linha %1: tamanho do registro não confere com byteCount.").arg(lineNum)};
        }

        uint8_t sum = static_cast<uint8_t>(byteCount + addrHi + addrLo + recType);
        QByteArray payload;
        payload.resize(byteCount);
        for (int i = 0; i < byteCount; ++i) {
            uint8_t b = 0;
            if (!parseHexByte(line.constData() + 9 + i * 2, b)) {
                return {false, {}, QStringLiteral("Linha %1: byte de dados inválido.").arg(lineNum)};
            }
            payload[i] = static_cast<char>(b);
            sum = static_cast<uint8_t>(sum + b);
        }

        uint8_t checksum = 0;
        if (!parseHexByte(line.constData() + 9 + byteCount * 2, checksum)) {
            return {false, {}, QStringLiteral("Linha %1: checksum inválido.").arg(lineNum)};
        }
        if (static_cast<uint8_t>(sum + checksum) != 0) {
            return {false, {}, QStringLiteral("Linha %1: falha no checksum Intel HEX.").arg(lineNum)};
        }

        const uint16_t offset16 = static_cast<uint16_t>((static_cast<uint16_t>(addrHi) << 8) | addrLo);

        switch (recType) {
        case 0x00: { // Data
            const uint64_t absAddr = baseAddr + offset16;
            const uint64_t endAddr = absAddr + static_cast<uint64_t>(byteCount);
            if (endAddr > 64ULL * 1024ULL * 1024ULL) {
                return {false, {}, QStringLiteral("Linha %1: endereço excede o limite de 64 MB.").arg(lineNum)};
            }
            maxEnd = std::max(maxEnd, endAddr);
            chunks.push_back({absAddr, std::move(payload)});
            break;
        }
        case 0x01: // EOF
            seenEof = true;
            break;
        case 0x02: { // Extended Segment Address
            if (byteCount != 2) {
                return {false, {}, QStringLiteral("Linha %1: registro tipo 02 inválido.").arg(lineNum)};
            }
            const uint16_t seg = static_cast<uint16_t>(
                (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]));
            baseAddr = static_cast<uint64_t>(seg) << 4;
            break;
        }
        case 0x04: { // Extended Linear Address
            if (byteCount != 2) {
                return {false, {}, QStringLiteral("Linha %1: registro tipo 04 inválido.").arg(lineNum)};
            }
            const uint16_t upper = static_cast<uint16_t>(
                (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]));
            baseAddr = static_cast<uint64_t>(upper) << 16;
            break;
        }
        case 0x03:
        case 0x05:
            // Start Segment / Linear Address — ignore
            break;
        default:
            return {false, {}, QStringLiteral("Linha %1: tipo de registro Intel HEX desconhecido (0x%2).")
                                   .arg(lineNum).arg(recType, 2, 16, QLatin1Char('0'))};
        }

        if (seenEof) {
            break;
        }
    }

    QByteArray out(static_cast<qsizetype>(maxEnd), static_cast<char>(0xFF));
    for (const auto &c : chunks) {
        std::copy(c.bytes.cbegin(), c.bytes.cend(), out.begin() + static_cast<qsizetype>(c.address));
    }

    return {true, std::move(out), {}};
}

QByteArray toIntelHex(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() * 3);

    uint16_t currentUpper = 0xFFFF;
    constexpr qsizetype kRecLen = 16;

    for (qsizetype offset = 0; offset < data.size(); offset += kRecLen) {
        const uint16_t upper = static_cast<uint16_t>((static_cast<uint64_t>(offset) >> 16) & 0xFFFFU);
        if (upper != currentUpper) {
            currentUpper = upper;
            const uint8_t hi = static_cast<uint8_t>(upper >> 8);
            const uint8_t lo = static_cast<uint8_t>(upper & 0xFFU);
            const uint8_t csum = static_cast<uint8_t>(-(2 + 0 + 0 + 4 + hi + lo));
            out += QStringLiteral(":02000004%1%2%3\r\n")
                       .arg(hi, 2, 16, QLatin1Char('0'))
                       .arg(lo, 2, 16, QLatin1Char('0'))
                       .arg(csum, 2, 16, QLatin1Char('0'))
                       .toUpper()
                       .toLatin1();
        }

        const qsizetype len = std::min(kRecLen, data.size() - offset);
        const uint16_t addr16 = static_cast<uint16_t>(offset & 0xFFFFU);
        const uint8_t ah = static_cast<uint8_t>(addr16 >> 8);
        const uint8_t al = static_cast<uint8_t>(addr16 & 0xFFU);

        uint8_t sum = static_cast<uint8_t>(len + ah + al);
        QString line = QStringLiteral(":%1%2%300")
                           .arg(static_cast<uint8_t>(len), 2, 16, QLatin1Char('0'))
                           .arg(ah, 2, 16, QLatin1Char('0'))
                           .arg(al, 2, 16, QLatin1Char('0'));

        for (qsizetype i = 0; i < len; ++i) {
            const uint8_t b = static_cast<uint8_t>(data[offset + i]);
            sum = static_cast<uint8_t>(sum + b);
            line += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
        }
        const uint8_t csum = static_cast<uint8_t>(-sum);
        line += QStringLiteral("%1\r\n").arg(csum, 2, 16, QLatin1Char('0'));
        out += line.toUpper().toLatin1();
    }

    out += ":00000001FF\r\n";
    return out;
}

uint32_t computeCrc32(const QByteArray &data)
{
    const auto &table = crc32Table();
    uint32_t crc = 0xFFFFFFFFU;
    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    const qsizetype n = data.size();
    for (qsizetype i = 0; i < n; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

QString computeCrc32Hex(const QByteArray &data)
{
    return QStringLiteral("%1").arg(computeCrc32(data), 8, 16, QLatin1Char('0')).toUpper();
}

QString computeMd5Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex()).toUpper();
}

bool parseHexPattern(const QString &text, QByteArray &outBytes)
{
    outBytes.clear();
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char(':') || ch == QLatin1Char(',')) {
            continue;
        }
        cleaned.append(ch);
    }

    if (cleaned.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        cleaned = cleaned.mid(2);
    }
    if (cleaned.isEmpty() || (cleaned.size() % 2) != 0) {
        return false;
    }

    outBytes.reserve(cleaned.size() / 2);
    for (qsizetype i = 0; i < cleaned.size(); i += 2) {
        const int hi = hexDigit(cleaned[i].toLatin1());
        const int lo = hexDigit(cleaned[i + 1].toLatin1());
        if (hi < 0 || lo < 0) {
            outBytes.clear();
            return false;
        }
        outBytes.append(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

} // namespace BufferUtils
