#include "ZifSvgGenerator.h"

#include <QString>

namespace {

QString xmlEscape(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

} // namespace

QByteArray ZifSvgGenerator::generate(const std::optional<ChipInfo> &chip,
                                     ChipPackage package,
                                     const ZifSvgTheme &theme)
{
    const bool hasChip = chip.has_value();
    const bool isEeprom = hasChip &&
                          (chip->rawType == QStringLiteral("24_EEPROM") ||
                           chip->rawType == QStringLiteral("93_EEPROM"));

    // Active rows in the 16-pin EZP2019 ZIF socket (rows 0..7):
    //   25 SPI Flash / 25 EEPROM -> upper half (rows 0..3 => ZIF pins 1..4 & 13..16)
    //   24 EEPROM / 93 EEPROM    -> lower half (rows 4..7 => ZIF pins 5..8 & 9..12)
    const int startRow = isEeprom ? 4 : 0;
    const int endRow = startRow + 3;
    const int pin1ZifNumber = startRow + 1; // Pin 1 (25xx) or Pin 5 (24/93xx)

    // Accent palette: blue for 25 SPI Flash, green for 24/93 EEPROM
    const QString accentHex = isEeprom ? QStringLiteral("#10b981") : QStringLiteral("#0ea5e9");
    const QString accentDarkHex = isEeprom ? QStringLiteral("#047857") : QStringLiteral("#0369a1");
    const QString accentSoftHex = isEeprom ? QStringLiteral("#d1fae5") : QStringLiteral("#e0f2fe");

    // Geometry in SVG viewBox (260 x 268)
    constexpr double kSocketX = 82.0;
    constexpr double kSocketY = 48.0;
    constexpr double kSocketW = 96.0;
    constexpr double kSocketH = 166.0;
    constexpr double kPinStartY = kSocketY + 18.0;
    constexpr double kPinStep = 18.5;
    constexpr double kLeftPinX = kSocketX + 16.0;
    constexpr double kRightPinX = kSocketX + kSocketW - 16.0;

    QString svg;
    svg.reserve(8192);

    svg += QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 260 268\" width=\"260\" height=\"268\">\n"
        "  <defs>\n"
        "    <linearGradient id=\"zifBody\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">\n"
        "      <stop offset=\"0%\" stop-color=\"#1e6b43\"/>\n"
        "      <stop offset=\"50%\" stop-color=\"#278252\"/>\n"
        "      <stop offset=\"100%\" stop-color=\"#1b5e3b\"/>\n"
        "    </linearGradient>\n"
        "    <linearGradient id=\"leverGrad\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
        "      <stop offset=\"0%\" stop-color=\"#e2e8f0\"/>\n"
        "      <stop offset=\"100%\" stop-color=\"#94a3b8\"/>\n"
        "    </linearGradient>\n"
        "  </defs>\n");

    // Card background
    svg += QStringLiteral(
        "  <rect x=\"2\" y=\"2\" width=\"256\" height=\"264\" rx=\"8\" fill=\"%1\" stroke=\"%2\" stroke-width=\"1.2\"/>\n")
               .arg(theme.cardBg.name(), theme.cardBorder.name());

    // Header title
    svg += QStringLiteral(
        "  <text x=\"130\" y=\"19\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"10.5\" "
        "font-weight=\"bold\" fill=\"%1\">Soquete ZIF 16 Pinos (Alavanca ▲)</text>\n")
               .arg(theme.primaryText.name());

    // Top ZIF locking lever bar
    svg += QStringLiteral(
        "  <rect x=\"%1\" y=\"%2\" width=\"26\" height=\"10\" rx=\"3\" fill=\"url(#leverGrad)\" stroke=\"#64748b\" stroke-width=\"1.2\"/>\n"
        "  <rect x=\"%3\" y=\"%4\" width=\"8\" height=\"15\" rx=\"2\" fill=\"#cbd5e1\" stroke=\"#64748b\" stroke-width=\"1\"/>\n"
        "  <circle cx=\"%5\" cy=\"%6\" r=\"4.5\" fill=\"#f1f5f9\" stroke=\"#475569\" stroke-width=\"1.2\"/>\n")
               .arg(kSocketX - 18.0, 0, 'f', 1)
               .arg(kSocketY - 17.0, 0, 'f', 1)
               .arg(kSocketX + 4.0, 0, 'f', 1)
               .arg(kSocketY - 12.0, 0, 'f', 1)
               .arg(kSocketX - 16.0, 0, 'f', 1)
               .arg(kSocketY - 12.0, 0, 'f', 1);

    // ZIF Socket green housing
    svg += QStringLiteral(
        "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"6\" fill=\"url(#zifBody)\" stroke=\"#14492d\" stroke-width=\"1.8\"/>\n")
               .arg(kSocketX, 0, 'f', 1)
               .arg(kSocketY, 0, 'f', 1)
               .arg(kSocketW, 0, 'f', 1)
               .arg(kSocketH, 0, 'f', 1);

    // Top ZIF orientation notch
    svg += QStringLiteral(
        "  <path d=\"M %1 %2 A 9 9 0 0 0 %3 %2 Z\" fill=\"#14492d\"/>\n")
               .arg(kSocketX + kSocketW / 2.0 - 9.0, 0, 'f', 1)
               .arg(kSocketY, 0, 'f', 1)
               .arg(kSocketX + kSocketW / 2.0 + 9.0, 0, 'f', 1);

    // Horizontal divider between 25xx (upper) and 24/93xx (lower)
    const double midY = kPinStartY + 3.5 * kPinStep;
    svg += QStringLiteral(
        "  <line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#86efac\" stroke-opacity=\"0.45\" stroke-width=\"1\" stroke-dasharray=\"3,3\"/>\n")
               .arg(kSocketX + 5.0, 0, 'f', 1)
               .arg(midY, 0, 'f', 1)
               .arg(kSocketX + kSocketW - 5.0, 0, 'f', 1);

    // Side zone labels on PCB
    svg += QStringLiteral(
        "  <text x=\"24\" y=\"%1\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"8.5\" font-weight=\"bold\" fill=\"%2\">25xx</text>\n"
        "  <text x=\"24\" y=\"%3\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"8.5\" font-weight=\"bold\" fill=\"%4\">24/93</text>\n")
               .arg(kPinStartY + 1.5 * kPinStep + 3.0, 0, 'f', 1)
               .arg(hasChip && !isEeprom ? accentDarkHex : theme.mutedText.name())
               .arg(kPinStartY + 5.5 * kPinStep + 3.0, 0, 'f', 1)
               .arg(hasChip && isEeprom ? accentDarkHex : theme.mutedText.name());

    // Highlight active pin zone inside ZIF
    if (hasChip) {
        const double zoneY = kPinStartY + startRow * kPinStep - 9.5;
        const double zoneH = 3.0 * kPinStep + 19.0;
        svg += QStringLiteral(
            "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"4\" fill=\"%5\" fill-opacity=\"0.22\" stroke=\"%5\" stroke-width=\"1.6\"/>\n")
                   .arg(kSocketX + 4.0, 0, 'f', 1)
                   .arg(zoneY, 0, 'f', 1)
                   .arg(kSocketW - 8.0, 0, 'f', 1)
                   .arg(zoneH, 0, 'f', 1)
                   .arg(accentHex);
    }

    // 16 ZIF pin holes + numbering (1..8 left, 16..9 right)
    for (int row = 0; row < 8; ++row) {
        const double cy = kPinStartY + row * kPinStep;
        const int leftNum = row + 1;
        const int rightNum = 16 - row;
        const bool active = hasChip && (row >= startRow && row <= endRow);

        const QString pinFill = active ? accentHex : QStringLiteral("#1e293b");
        const QString pinStroke = active ? QStringLiteral("#ffffff") : QStringLiteral("#0f3d24");
        const QString numFill = active ? accentDarkHex : theme.mutedText.name();
        const QString numWeight = active ? QStringLiteral("bold") : QStringLiteral("normal");

        if (active) {
            svg += QStringLiteral(
                "  <circle cx=\"%1\" cy=\"%2\" r=\"6.2\" fill=\"%3\" fill-opacity=\"0.35\"/>\n"
                "  <circle cx=\"%4\" cy=\"%2\" r=\"6.2\" fill=\"%3\" fill-opacity=\"0.35\"/>\n")
                       .arg(kLeftPinX, 0, 'f', 1)
                       .arg(cy, 0, 'f', 1)
                       .arg(accentHex)
                       .arg(kRightPinX, 0, 'f', 1);
        }

        svg += QStringLiteral(
            "  <circle cx=\"%1\" cy=\"%2\" r=\"3.6\" fill=\"%3\" stroke=\"%4\" stroke-width=\"1.2\"/>\n"
            "  <circle cx=\"%5\" cy=\"%2\" r=\"3.6\" fill=\"%3\" stroke=\"%4\" stroke-width=\"1.2\"/>\n"
            "  <text x=\"%6\" y=\"%7\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"9.5\" font-weight=\"%8\" fill=\"%9\">%10</text>\n"
            "  <text x=\"%11\" y=\"%7\" text-anchor=\"start\" font-family=\"monospace\" font-size=\"9.5\" font-weight=\"%8\" fill=\"%9\">%12</text>\n")
                   .arg(kLeftPinX, 0, 'f', 1)
                   .arg(cy, 0, 'f', 1)
                   .arg(pinFill, pinStroke)
                   .arg(kRightPinX, 0, 'f', 1)
                   .arg(kSocketX - 6.0, 0, 'f', 1)
                   .arg(cy + 3.3, 0, 'f', 1)
                   .arg(numWeight, numFill)
                   .arg(leftNum)
                   .arg(kSocketX + kSocketW + 6.0, 0, 'f', 1)
                   .arg(rightNum);
    }

    // Draw IC (DIP8 directly or SOIC8 on adapter board)
    if (hasChip) {
        const double topPinY = kPinStartY + startRow * kPinStep;
        const double icTop = topPinY - 6.5;
        const double icHeight = 3.0 * kPinStep + 13.0;
        const QString chipLabel = xmlEscape(chip->name);

        if (package == ChipPackage::Dip8) {
            const double icX = kLeftPinX + 9.0;
            const double icW = (kRightPinX - kLeftPinX) - 18.0;

            // Metallic DIP8 legs
            for (int i = 0; i < 4; ++i) {
                const double cy = kPinStartY + (startRow + i) * kPinStep;
                svg += QStringLiteral(
                    "  <line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#cbd5e1\" stroke-width=\"3\" stroke-linecap=\"round\"/>\n"
                    "  <line x1=\"%4\" y1=\"%2\" x2=\"%5\" y2=\"%2\" stroke=\"#cbd5e1\" stroke-width=\"3\" stroke-linecap=\"round\"/>\n")
                           .arg(kLeftPinX, 0, 'f', 1)
                           .arg(cy, 0, 'f', 1)
                           .arg(icX, 0, 'f', 1)
                           .arg(icX + icW, 0, 'f', 1)
                           .arg(kRightPinX, 0, 'f', 1);
            }

            // DIP8 black epoxy package
            svg += QStringLiteral(
                "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"3\" fill=\"#1e293b\" stroke=\"%5\" stroke-width=\"1.4\"/>\n")
                       .arg(icX, 0, 'f', 1)
                       .arg(icTop, 0, 'f', 1)
                       .arg(icW, 0, 'f', 1)
                       .arg(icHeight, 0, 'f', 1)
                       .arg(accentHex);

            // Top semicircular orientation notch (chanfro)
            svg += QStringLiteral(
                "  <path d=\"M %1 %2 A 6 6 0 0 0 %3 %2 Z\" fill=\"%4\"/>\n")
                       .arg(icX + icW / 2.0 - 6.0, 0, 'f', 1)
                       .arg(icTop, 0, 'f', 1)
                       .arg(icX + icW / 2.0 + 6.0, 0, 'f', 1)
                       .arg(accentHex);

            // Pin 1 orientation dot (bolinha)
            svg += QStringLiteral(
                "  <circle cx=\"%1\" cy=\"%2\" r=\"3.2\" fill=\"#facc15\" stroke=\"#854d0e\" stroke-width=\"0.8\"/>\n")
                       .arg(icX + 8.0, 0, 'f', 1)
                       .arg(icTop + 9.0, 0, 'f', 1);

            // Chip name on body
            svg += QStringLiteral(
                "  <text x=\"%1\" y=\"%2\" text-anchor=\"middle\" font-family=\"monospace\" font-size=\"8\" font-weight=\"bold\" fill=\"#f8fafc\">%3</text>\n"
                "  <text x=\"%1\" y=\"%4\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"7\" fill=\"#94a3b8\">DIP-8</text>\n")
                       .arg(icX + icW / 2.0, 0, 'f', 1)
                       .arg(icTop + icHeight / 2.0 + 1.0, 0, 'f', 1)
                       .arg(chipLabel)
                       .arg(icTop + icHeight / 2.0 + 13.0, 0, 'f', 1);
        } else {
            // SOIC8 Adapter PCB seated on the 8 highlighted ZIF pins
            const double pcbX = kLeftPinX - 4.0;
            const double pcbW = (kRightPinX - kLeftPinX) + 8.0;
            const double pcbY = icTop - 3.0;
            const double pcbH = icHeight + 6.0;

            svg += QStringLiteral(
                "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"4\" fill=\"%5\" stroke=\"%6\" stroke-width=\"1.6\"/>\n")
                       .arg(pcbX, 0, 'f', 1)
                       .arg(pcbY, 0, 'f', 1)
                       .arg(pcbW, 0, 'f', 1)
                       .arg(pcbH, 0, 'f', 1)
                       .arg(accentSoftHex, accentDarkHex);

            // PCB through-hole header pins aligned with ZIF pins
            for (int i = 0; i < 4; ++i) {
                const double cy = kPinStartY + (startRow + i) * kPinStep;
                svg += QStringLiteral(
                    "  <circle cx=\"%1\" cy=\"%2\" r=\"3.2\" fill=\"#f59e0b\" stroke=\"#92400e\" stroke-width=\"0.9\"/>\n"
                    "  <circle cx=\"%3\" cy=\"%2\" r=\"3.2\" fill=\"#f59e0b\" stroke=\"#92400e\" stroke-width=\"0.9\"/>\n")
                           .arg(kLeftPinX, 0, 'f', 1)
                           .arg(cy, 0, 'f', 1)
                           .arg(kRightPinX, 0, 'f', 1);
            }

            // Centered SOIC-8 SMD chip on adapter
            const double smdW = 30.0;
            const double smdH = 38.0;
            const double smdX = kSocketX + (kSocketW - smdW) / 2.0;
            const double smdY = pcbY + (pcbH - smdH) / 2.0;

            // SMD gull-wing leads
            for (int i = 0; i < 4; ++i) {
                const double ly = smdY + 6.5 + i * 8.3;
                svg += QStringLiteral(
                    "  <line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#94a3b8\" stroke-width=\"2.4\"/>\n"
                    "  <line x1=\"%4\" y1=\"%2\" x2=\"%5\" y2=\"%2\" stroke=\"#94a3b8\" stroke-width=\"2.4\"/>\n")
                           .arg(smdX - 6.0, 0, 'f', 1)
                           .arg(ly, 0, 'f', 1)
                           .arg(smdX, 0, 'f', 1)
                           .arg(smdX + smdW, 0, 'f', 1)
                           .arg(smdX + smdW + 6.0, 0, 'f', 1);
            }

            svg += QStringLiteral(
                "  <rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"2\" fill=\"#0f172a\" stroke=\"#334155\" stroke-width=\"1\"/>\n"
                "  <path d=\"M %5 %2 A 4 4 0 0 0 %6 %2 Z\" fill=\"%7\"/>\n"
                "  <circle cx=\"%8\" cy=\"%9\" r=\"2.4\" fill=\"#facc15\"/>\n"
                "  <text x=\"%10\" y=\"%11\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"6.8\" font-weight=\"bold\" fill=\"#f8fafc\">SOIC8</text>\n"
                "  <text x=\"%10\" y=\"%12\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"6.8\" font-weight=\"bold\" fill=\"%13\">ADAPT.</text>\n")
                       .arg(smdX, 0, 'f', 1)
                       .arg(smdY, 0, 'f', 1)
                       .arg(smdW, 0, 'f', 1)
                       .arg(smdH, 0, 'f', 1)
                       .arg(smdX + smdW / 2.0 - 4.0, 0, 'f', 1)
                       .arg(smdX + smdW / 2.0 + 4.0, 0, 'f', 1)
                       .arg(accentHex)
                       .arg(smdX + 6.0, 0, 'f', 1)
                       .arg(smdY + 7.0, 0, 'f', 1)
                       .arg(smdX + smdW / 2.0, 0, 'f', 1)
                       .arg(smdY + smdH / 2.0 + 4.0, 0, 'f', 1)
                       .arg(pcbY + pcbH - 4.0, 0, 'f', 1)
                       .arg(accentDarkHex);
        }

        // Pin 1 callout badge pointing to the exact ZIF Pin 1 position
        const double calloutY = topPinY;
        svg += QStringLiteral(
            "  <rect x=\"8\" y=\"%1\" width=\"46\" height=\"16\" rx=\"4\" fill=\"#facc15\" stroke=\"#a16207\" stroke-width=\"1\"/>\n"
            "  <text x=\"31\" y=\"%2\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"8\" font-weight=\"bold\" fill=\"#422006\">PINO 1</text>\n"
            "  <line x1=\"54\" y1=\"%3\" x2=\"%4\" y2=\"%3\" stroke=\"#ca8a04\" stroke-width=\"1.4\"/>\n"
            "  <polygon points=\"%4,%3 %5,%6 %5,%7\" fill=\"#ca8a04\"/>\n")
                   .arg(calloutY - 8.0, 0, 'f', 1)
                   .arg(calloutY + 3.0, 0, 'f', 1)
                   .arg(calloutY, 0, 'f', 1)
                   .arg(kSocketX - 18.0, 0, 'f', 1)
                   .arg(kSocketX - 23.0, 0, 'f', 1)
                   .arg(calloutY - 3.0, 0, 'f', 1)
                   .arg(calloutY + 3.0, 0, 'f', 1);
    }

    // Footer instruction text
    const QString footerText = hasChip
        ? QStringLiteral("Pinos ZIF %1–%2 e %3–%4 | Pino 1 no ZIF %5")
              .arg(startRow + 1)
              .arg(endRow + 1)
              .arg(16 - endRow)
              .arg(16 - startRow)
              .arg(pin1ZifNumber)
        : QStringLiteral("Selecione um chip para ver o encaixe");

    svg += QStringLiteral(
        "  <text x=\"130\" y=\"232\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"9.2\" font-weight=\"bold\" fill=\"%1\">%2</text>\n")
               .arg(hasChip ? accentDarkHex : theme.mutedText.name(), xmlEscape(footerText));

    // Optional 1.8V warning pill or package subtitle
    if (hasChip && chip->is1V8()) {
        svg += QStringLiteral(
            "  <rect x=\"36\" y=\"241\" width=\"188\" height=\"18\" rx=\"4\" fill=\"#d97706\"/>\n"
            "  <text x=\"130\" y=\"253\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"8.8\" font-weight=\"bold\" fill=\"#ffffff\">⚠ Requer adaptador 1.8V</text>\n");
    } else if (hasChip) {
        const QString pkgNote = (package == ChipPackage::Soic8Adapter)
            ? QStringLiteral("Modo: SOIC-8 com adaptador DIP-8")
            : QStringLiteral("Modo: DIP-8 direto no soquete ZIF");
        svg += QStringLiteral(
            "  <text x=\"130\" y=\"252\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"8.5\" fill=\"%1\">%2</text>\n")
                   .arg(theme.mutedText.name(), xmlEscape(pkgNote));
    }

    svg += QStringLiteral("</svg>\n");
    return svg.toUtf8();
}
