#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <optional>

struct ChipInfo {
    std::size_t index = 0;
    QString rawType;      // e.g. "SPI_FLASH", "24_EEPROM", "93_EEPROM"
    QString displayType;  // e.g. "SPI Flash", "24 EEPROM", "25 EEPROM", "93 EEPROM"
    QString manufacturer;
    QString name;
    uint32_t chipId = 0;
    uint32_t size = 0;
    uint16_t pageSize = 0;
    uint16_t protocolCfg = 0;
    uint32_t flags = 0;

    bool is1V8() const { return (flags & 0x01000000U) != 0; }
    bool supportsErase() const { return (flags & 0x00010000U) != 0; }
    bool supportsSectorErase() const { return (flags & 0x00040000U) != 0; }

    QString formattedSize() const;
    QString formattedCapacity() const;
    QString protocolName() const;
    QString voltageLabel() const;
};

class ChipDatabase {
public:
    ChipDatabase();

    const QVector<ChipInfo> &allChips() const { return m_chips; }

    QStringList memoryTypes() const;
    QStringList manufacturersForType(const QString &displayType) const;
    QVector<ChipInfo> chipsFor(const QString &displayType, const QString &manufacturer) const;

    QVector<ChipInfo> searchByCode(const QString &query) const;
    std::optional<ChipInfo> findByChipId(uint32_t chipId) const;
    QVector<ChipInfo> findAllByChipId(uint32_t chipId) const;
    std::optional<ChipInfo> findByIndex(std::size_t index) const;

    static QString toDisplayType(const QString &rawType);

private:
    QVector<ChipInfo> m_chips;
};
