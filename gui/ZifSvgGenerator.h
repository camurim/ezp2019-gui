#pragma once

#include "ChipDatabase.h"

#include <QByteArray>
#include <QColor>
#include <optional>

enum class ChipPackage {
    Dip8,
    Soic8Adapter
};

struct ZifSvgTheme {
    QColor cardBg = QColor(248, 250, 252);
    QColor cardBorder = QColor(203, 213, 225);
    QColor primaryText = QColor(30, 41, 59);
    QColor mutedText = QColor(100, 116, 139);
};

class ZifSvgGenerator {
public:
    static QByteArray generate(const std::optional<ChipInfo> &chip,
                               ChipPackage package,
                               const ZifSvgTheme &theme = {});
};
