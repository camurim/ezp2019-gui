#include "ChipDatabase.h"

#include "ezp2019.h"

#include <QSet>
#include <algorithm>

QString ChipInfo::formattedSize() const
{
    if (size >= 1024U * 1024U) {
        const double mb = static_cast<double>(size) / (1024.0 * 1024.0);
        const double mbit = mb * 8.0;
        return QStringLiteral("%1 MB (%2 Mbit)").arg(mb, 0, 'g', 4).arg(mbit, 0, 'g', 4);
    }
    if (size >= 1024U) {
        const double kb = static_cast<double>(size) / 1024.0;
        const double kbit = kb * 8.0;
        return QStringLiteral("%1 KB (%2 Kbit)").arg(kb, 0, 'g', 4).arg(kbit, 0, 'g', 4);
    }
    return QStringLiteral("%1 B (%2 bit)").arg(size).arg(size * 8U);
}

QString ChipInfo::formattedCapacity() const
{
    if (size >= 1024U * 1024U) {
        const double mb = static_cast<double>(size) / (1024.0 * 1024.0);
        return QStringLiteral("%1 MB (%2 Bytes)").arg(mb, 0, 'g', 4).arg(size);
    }
    if (size >= 1024U) {
        const double kb = static_cast<double>(size) / 1024.0;
        return QStringLiteral("%1 KB (%2 Bytes)").arg(kb, 0, 'g', 4).arg(size);
    }
    return QStringLiteral("%1 Bytes").arg(size);
}

QString ChipInfo::protocolName() const
{
    if (rawType == QStringLiteral("24_EEPROM")) {
        return QStringLiteral("I²C (24xx)");
    }
    if (rawType == QStringLiteral("93_EEPROM")) {
        return QStringLiteral("Microwire (93xx)");
    }
    const uint8_t proto = static_cast<uint8_t>(protocolCfg & 0xFFU);
    if (proto == 0x06U) {
        return QStringLiteral("SPI (4-Byte)");
    }
    return QStringLiteral("SPI (25xx)");
}

QString ChipInfo::voltageLabel() const
{
    if (is1V8()) {
        return QStringLiteral("1.8V (requer adaptador 1.8V)");
    }
    if (rawType == QStringLiteral("24_EEPROM") || rawType == QStringLiteral("93_EEPROM")) {
        return QStringLiteral("3.3V / 5.0V (EEPROM)");
    }
    return QStringLiteral("3.3V");
}

QString ChipDatabase::toDisplayType(const QString &rawType)
{
    if (rawType == QStringLiteral("SPI_FLASH")) {
        return QStringLiteral("SPI Flash");
    }
    if (rawType == QStringLiteral("24_EEPROM")) {
        return QStringLiteral("24 EEPROM");
    }
    if (rawType == QStringLiteral("25_EEPROM")) {
        return QStringLiteral("25 EEPROM");
    }
    if (rawType == QStringLiteral("93_EEPROM")) {
        return QStringLiteral("93 EEPROM");
    }
    return rawType;
}

ChipDatabase::ChipDatabase()
{
    const std::size_t count = exp2019_get_num_chips();
    m_chips.reserve(static_cast<qsizetype>(count));

    for (std::size_t i = 0; i < count; ++i) {
        ChipInfo info;
        info.index = i;
        info.rawType = QString::fromUtf8(exp2019_get_chip_type_by_index(i));
        info.displayType = toDisplayType(info.rawType);
        info.manufacturer = QString::fromUtf8(exp2019_get_manufacturer_by_index(i));
        info.name = QString::fromUtf8(exp2019_get_chip_name_by_index(i));
        info.chipId = exp2019_get_ic_id(i);
        info.size = exp2019_get_chip_size_by_index(i);
        info.pageSize = exp2019_get_chip_pagesize_by_index(i);
        info.protocolCfg = exp2019_get_chip_protocol_enum_cfg_by_index(i);
        info.flags = exp2019_get_chip_flags_by_index(i);
        m_chips.push_back(std::move(info));
    }
}

QStringList ChipDatabase::memoryTypes() const
{
    // Standard EZP2019 memory families in familiar order
    QStringList types = {
        QStringLiteral("SPI Flash"),
        QStringLiteral("24 EEPROM"),
        QStringLiteral("25 EEPROM"),
        QStringLiteral("93 EEPROM")
    };

    for (const auto &chip : m_chips) {
        if (!types.contains(chip.displayType)) {
            types.push_back(chip.displayType);
        }
    }
    return types;
}

QStringList ChipDatabase::manufacturersForType(const QString &displayType) const
{
    QSet<QString> unique;
    for (const auto &chip : m_chips) {
        if (chip.displayType == displayType) {
            unique.insert(chip.manufacturer);
        }
    }
    QStringList list(unique.begin(), unique.end());
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return list;
}

QVector<ChipInfo> ChipDatabase::chipsFor(const QString &displayType, const QString &manufacturer) const
{
    QVector<ChipInfo> result;
    for (const auto &chip : m_chips) {
        if (chip.displayType == displayType && chip.manufacturer == manufacturer) {
            result.push_back(chip);
        }
    }
    return result;
}

QVector<ChipInfo> ChipDatabase::searchByCode(const QString &query) const
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString hexQuery = trimmed;
    if (hexQuery.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexQuery = hexQuery.mid(2);
    }

    QVector<ChipInfo> result;
    for (const auto &chip : m_chips) {
        const QString idHex = QStringLiteral("%1").arg(chip.chipId, 6, 16, QLatin1Char('0'));
        if (chip.name.contains(trimmed, Qt::CaseInsensitive) ||
            chip.manufacturer.contains(trimmed, Qt::CaseInsensitive) ||
            (chip.chipId != 0 && !hexQuery.isEmpty() && idHex.contains(hexQuery, Qt::CaseInsensitive))) {
            result.push_back(chip);
        }
    }
    return result;
}

std::optional<ChipInfo> ChipDatabase::findByChipId(uint32_t chipId) const
{
    const auto matches = findAllByChipId(chipId);
    if (matches.isEmpty()) {
        return std::nullopt;
    }
    return matches.first();
}

QVector<ChipInfo> ChipDatabase::findAllByChipId(uint32_t chipId) const
{
    QVector<ChipInfo> matches;
    if (chipId == 0 || chipId == 0xFFFFFFU || chipId == EXP2019_INVALID_CHIP_ID) {
        return matches;
    }
    for (const auto &chip : m_chips) {
        if (chip.chipId == chipId) {
            matches.push_back(chip);
        }
    }
    return matches;
}

std::optional<ChipInfo> ChipDatabase::findByIndex(std::size_t index) const
{
    if (index >= static_cast<std::size_t>(m_chips.size())) {
        return std::nullopt;
    }
    return m_chips[static_cast<qsizetype>(index)];
}
