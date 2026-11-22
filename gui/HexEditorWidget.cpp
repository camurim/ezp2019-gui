#include "HexEditorWidget.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>
#include <limits>

HexEditorWidget::HexEditorWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (mono.pointSize() < 10) {
        mono.setPointSize(10);
    }
    setFont(mono);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setBackgroundRole(QPalette::Base);
    viewport()->setAutoFillBackground(true);

    updateMetrics();
    updateScrollBars();
}

void HexEditorWidget::setData(const QByteArray &data)
{
    m_data = data;
    m_modifiedOffsets.clear();
    m_highlightOffset = -1;
    m_highlightLength = 0;
    m_cursorOffset = 0;
    m_lowNibble = false;
    m_modified = false;
    updateScrollBars();
    verticalScrollBar()->setValue(0);
    viewport()->update();
    emit cursorChanged(0, !m_data.isEmpty(), m_data.isEmpty() ? 0 : static_cast<uint8_t>(m_data[0]));
}

void HexEditorWidget::setData(QByteArray &&data)
{
    m_data = std::move(data);
    m_modifiedOffsets.clear();
    m_highlightOffset = -1;
    m_highlightLength = 0;
    m_cursorOffset = 0;
    m_lowNibble = false;
    m_modified = false;
    updateScrollBars();
    verticalScrollBar()->setValue(0);
    viewport()->update();
    emit cursorChanged(0, !m_data.isEmpty(), m_data.isEmpty() ? 0 : static_cast<uint8_t>(m_data[0]));
}

void HexEditorWidget::clear()
{
    setData(QByteArray{});
}

void HexEditorWidget::fill(uint8_t value, qsizetype size)
{
    QByteArray buf(size, static_cast<char>(value));
    setData(std::move(buf));
}

void HexEditorWidget::fillExisting(uint8_t value)
{
    if (m_data.isEmpty()) {
        return;
    }
    m_data.fill(static_cast<char>(value));
    m_modifiedOffsets.clear();
    m_highlightOffset = -1;
    m_highlightLength = 0;
    m_modified = false;
    viewport()->update();
    emit cursorChanged(m_cursorOffset, true, value);
}

void HexEditorWidget::setModified(bool modified)
{
    m_modified = modified;
    if (!modified) {
        m_modifiedOffsets.clear();
        viewport()->update();
    }
}

qsizetype HexEditorWidget::findNext(const QByteArray &pattern)
{
    if (m_data.isEmpty() || pattern.isEmpty() || pattern.size() > m_data.size()) {
        return -1;
    }

    qsizetype start = (m_highlightOffset == m_cursorOffset) ? (m_cursorOffset + 1) : m_cursorOffset;
    if (start >= m_data.size()) {
        start = 0;
    }

    qsizetype found = m_data.indexOf(pattern, start);
    if (found < 0 && start > 0) {
        found = m_data.indexOf(pattern, 0);
    }

    if (found >= 0) {
        m_highlightOffset = found;
        m_highlightLength = pattern.size();
        setCursorOffset(found);
    }
    return found;
}

qsizetype HexEditorWidget::findPrevious(const QByteArray &pattern)
{
    if (m_data.isEmpty() || pattern.isEmpty() || pattern.size() > m_data.size()) {
        return -1;
    }

    qsizetype start = (m_cursorOffset > 0) ? (m_cursorOffset - 1) : (m_data.size() - 1);
    qsizetype found = m_data.lastIndexOf(pattern, start);
    if (found < 0) {
        found = m_data.lastIndexOf(pattern, m_data.size() - 1);
    }

    if (found >= 0) {
        m_highlightOffset = found;
        m_highlightLength = pattern.size();
        setCursorOffset(found);
    }
    return found;
}

void HexEditorWidget::clearHighlight()
{
    m_highlightOffset = -1;
    m_highlightLength = 0;
    viewport()->update();
}

void HexEditorWidget::updateMetrics()
{
    const QFontMetrics fm(font());
    m_metrics.charWidth = std::max(1, fm.horizontalAdvance(QLatin1Char('0')));
    m_metrics.rowHeight = fm.height() + 4;
    m_metrics.headerHeight = m_metrics.rowHeight + 6;
    m_metrics.addrX = m_metrics.charWidth;
    m_metrics.hexX = m_metrics.addrX + m_metrics.charWidth * 10;
    m_metrics.asciiX = m_metrics.hexX + m_metrics.charWidth * (kBytesPerRow * 3 + 2);
    m_metrics.totalWidth = m_metrics.asciiX + m_metrics.charWidth * (kBytesPerRow + 2);
}

qsizetype HexEditorWidget::rowCount() const
{
    if (m_data.isEmpty()) {
        return 0;
    }
    return (m_data.size() + kBytesPerRow - 1) / kBytesPerRow;
}

int HexEditorWidget::byteHexX(int col) const
{
    const int extraGap = (col >= 8) ? m_metrics.charWidth : 0;
    return m_metrics.hexX + col * (m_metrics.charWidth * 3) + extraGap;
}

void HexEditorWidget::updateScrollBars()
{
    const int visibleHeight = std::max(0, viewport()->height() - m_metrics.headerHeight);
    const int visibleRows = std::max(1, visibleHeight / m_metrics.rowHeight);
    const qsizetype totalRows = rowCount();

    const qsizetype maxScroll = std::max<qsizetype>(0, totalRows - visibleRows);
    const int clampedMax = static_cast<int>(
        std::min<qsizetype>(maxScroll, std::numeric_limits<int>::max()));

    verticalScrollBar()->setRange(0, clampedMax);
    verticalScrollBar()->setPageStep(visibleRows);
    verticalScrollBar()->setSingleStep(1);

    const int hMax = std::max(0, m_metrics.totalWidth - viewport()->width());
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(m_metrics.charWidth * 2);
}

void HexEditorWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void HexEditorWidget::ensureCursorVisible()
{
    if (m_data.isEmpty()) {
        return;
    }
    const int row = static_cast<int>(m_cursorOffset / kBytesPerRow);
    const int firstRow = verticalScrollBar()->value();
    const int visibleRows = std::max(1, (viewport()->height() - m_metrics.headerHeight) / m_metrics.rowHeight);

    if (row < firstRow) {
        verticalScrollBar()->setValue(row);
    } else if (row >= firstRow + visibleRows) {
        verticalScrollBar()->setValue(row - visibleRows + 1);
    }
}

void HexEditorWidget::setCursorOffset(qsizetype offset)
{
    if (m_data.isEmpty()) {
        return;
    }
    m_cursorOffset = std::clamp<qsizetype>(offset, 0, m_data.size() - 1);
    m_lowNibble = false;
    ensureCursorVisible();
    viewport()->update();
    emit cursorChanged(m_cursorOffset, true, static_cast<uint8_t>(m_data[m_cursorOffset]));
}

void HexEditorWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(font());

    const QPalette &pal = palette();
    const int xOffset = horizontalScrollBar()->value();
    const int viewW = viewport()->width();
    const int viewH = viewport()->height();

    // Header background
    p.fillRect(0, 0, viewW, m_metrics.headerHeight, pal.window());
    p.setPen(pal.mid().color());
    p.drawLine(0, m_metrics.headerHeight - 1, viewW, m_metrics.headerHeight - 1);

    // Header labels
    QFont headerFont = font();
    headerFont.setBold(true);
    p.setFont(headerFont);
    p.setPen(pal.windowText().color());

    const int headerBaseline = m_metrics.headerHeight - 6;
    p.drawText(m_metrics.addrX - xOffset, headerBaseline, QStringLiteral("Offset"));
    for (int col = 0; col < kBytesPerRow; ++col) {
        p.drawText(byteHexX(col) - xOffset, headerBaseline,
                   QStringLiteral("%1").arg(col, 2, 16, QLatin1Char('0')).toUpper());
    }
    p.drawText(m_metrics.asciiX - xOffset, headerBaseline, QStringLiteral("ASCII"));

    p.setFont(font());

    if (m_data.isEmpty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRect(0, m_metrics.headerHeight, viewW, viewH - m_metrics.headerHeight),
                   Qt::AlignCenter,
                   QStringLiteral("Buffer vazio — selecione um chip, abra um arquivo (.bin/.hex) ou clique em \"Ler Chip\"."));
        return;
    }

    const qsizetype firstRow = verticalScrollBar()->value();
    const int visibleRows = (viewH - m_metrics.headerHeight) / m_metrics.rowHeight + 2;
    const qsizetype lastRow = std::min(rowCount(), firstRow + visibleRows);

    const QColor altRowColor = pal.alternateBase().color();
    const QColor addrColor = pal.color(QPalette::PlaceholderText);
    const QColor textColor = pal.text().color();
    const QColor modifiedColor(220, 38, 38); // Crisp red for edited bytes
    const QColor searchBg(250, 204, 21, 95); // Soft amber highlight for search match
    const QColor highlightBg = pal.highlight().color();
    const QColor highlightFg = pal.highlightedText().color();
    QColor secondaryHighlight = highlightBg;
    secondaryHighlight.setAlpha(70);

    static const char kHexDigits[] = "0123456789ABCDEF";
    const QFontMetrics fm(font());
    const int ascent = fm.ascent();

    // Vertical separators
    p.setPen(pal.mid().color());
    const int sep1 = m_metrics.hexX - m_metrics.charWidth - xOffset;
    const int sep2 = m_metrics.asciiX - m_metrics.charWidth - xOffset;
    p.drawLine(sep1, 0, sep1, viewH);
    p.drawLine(sep2, 0, sep2, viewH);

    for (qsizetype row = firstRow; row < lastRow; ++row) {
        const int y = m_metrics.headerHeight + static_cast<int>(row - firstRow) * m_metrics.rowHeight;
        const int baseline = y + ascent + 2;

        if ((row & 1) == 1) {
            p.fillRect(0, y, viewW, m_metrics.rowHeight, altRowColor);
        }

        const qsizetype rowOffset = row * kBytesPerRow;
        const QString addrStr = QStringLiteral("%1").arg(rowOffset, 8, 16, QLatin1Char('0')).toUpper();
        p.setPen(addrColor);
        p.drawText(m_metrics.addrX - xOffset, baseline, addrStr);

        const qsizetype bytesInRow = std::min<qsizetype>(kBytesPerRow, m_data.size() - rowOffset);
        for (int col = 0; col < bytesInRow; ++col) {
            const qsizetype byteOffset = rowOffset + col;
            const uint8_t b = static_cast<uint8_t>(m_data[byteOffset]);
            const bool isCursor = (byteOffset == m_cursorOffset);
            const bool isEdited = m_modifiedOffsets.contains(byteOffset);
            const bool isSearchMatch = (m_highlightOffset >= 0 &&
                                        byteOffset >= m_highlightOffset &&
                                        byteOffset < m_highlightOffset + m_highlightLength);

            const int hx = byteHexX(col) - xOffset;
            const int ax = m_metrics.asciiX + col * m_metrics.charWidth - xOffset;
            const QRect hexRect(hx - 1, y, m_metrics.charWidth * 2 + 2, m_metrics.rowHeight);
            const QRect asciiRect(ax, y, m_metrics.charWidth, m_metrics.rowHeight);

            if (isSearchMatch) {
                p.fillRect(hexRect, searchBg);
                p.fillRect(asciiRect, searchBg);
            }

            if (isCursor) {
                p.fillRect(hexRect, m_inAsciiPane ? secondaryHighlight : highlightBg);
                p.fillRect(asciiRect, m_inAsciiPane ? highlightBg : secondaryHighlight);
            }

            const QColor baseFg = isEdited ? modifiedColor : textColor;

            char hexBuf[3] = {kHexDigits[b >> 4], kHexDigits[b & 0x0F], '\0'};
            p.setPen((isCursor && !m_inAsciiPane) ? highlightFg : baseFg);
            p.drawText(hx, baseline, QLatin1String(hexBuf, 2));

            const char ch = (b >= 0x20 && b <= 0x7E) ? static_cast<char>(b) : '.';
            p.setPen((isCursor && m_inAsciiPane) ? highlightFg : baseFg);
            p.drawText(ax, baseline, QString(QLatin1Char(ch)));
        }
    }
}

void HexEditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_data.isEmpty() || event->button() != Qt::LeftButton) {
        return;
    }

    const int x = static_cast<int>(event->position().x()) + horizontalScrollBar()->value();
    const int y = static_cast<int>(event->position().y());
    if (y < m_metrics.headerHeight) {
        return;
    }

    const qsizetype row = verticalScrollBar()->value() + (y - m_metrics.headerHeight) / m_metrics.rowHeight;
    if (row < 0 || row >= rowCount()) {
        return;
    }

    int col = -1;
    if (x >= m_metrics.asciiX && x < m_metrics.asciiX + kBytesPerRow * m_metrics.charWidth) {
        col = (x - m_metrics.asciiX) / m_metrics.charWidth;
        m_inAsciiPane = true;
    } else if (x >= m_metrics.hexX && x < m_metrics.asciiX - m_metrics.charWidth) {
        m_inAsciiPane = false;
        for (int c = 0; c < kBytesPerRow; ++c) {
            const int bx = byteHexX(c);
            if (x >= bx - m_metrics.charWidth / 2 && x < bx + m_metrics.charWidth * 3) {
                col = c;
                break;
            }
        }
    }

    if (col >= 0 && col < kBytesPerRow) {
        const qsizetype target = row * kBytesPerRow + col;
        if (target < m_data.size()) {
            setCursorOffset(target);
        }
    }
}

void HexEditorWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_data.isEmpty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const int visibleRows = std::max(1, (viewport()->height() - m_metrics.headerHeight) / m_metrics.rowHeight);

    switch (event->key()) {
    case Qt::Key_Left:
        setCursorOffset(m_cursorOffset - 1);
        return;
    case Qt::Key_Right:
        setCursorOffset(m_cursorOffset + 1);
        return;
    case Qt::Key_Up:
        setCursorOffset(m_cursorOffset - kBytesPerRow);
        return;
    case Qt::Key_Down:
        setCursorOffset(m_cursorOffset + kBytesPerRow);
        return;
    case Qt::Key_PageUp:
        setCursorOffset(m_cursorOffset - static_cast<qsizetype>(visibleRows) * kBytesPerRow);
        return;
    case Qt::Key_PageDown:
        setCursorOffset(m_cursorOffset + static_cast<qsizetype>(visibleRows) * kBytesPerRow);
        return;
    case Qt::Key_Home:
        if (event->modifiers() & Qt::ControlModifier) {
            setCursorOffset(0);
        } else {
            setCursorOffset((m_cursorOffset / kBytesPerRow) * kBytesPerRow);
        }
        return;
    case Qt::Key_End:
        if (event->modifiers() & Qt::ControlModifier) {
            setCursorOffset(m_data.size() - 1);
        } else {
            setCursorOffset(std::min(m_data.size() - 1, (m_cursorOffset / kBytesPerRow) * kBytesPerRow + kBytesPerRow - 1));
        }
        return;
    case Qt::Key_Tab:
        m_inAsciiPane = !m_inAsciiPane;
        m_lowNibble = false;
        viewport()->update();
        return;
    default:
        break;
    }

    // In-place editing (marks edited byte in red)
    const QString text = event->text();
    if (text.size() == 1 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        const QChar ch = text.at(0);
        if (m_inAsciiPane) {
            if (ch.unicode() >= 0x20 && ch.unicode() <= 0x7E) {
                const uint8_t newVal = static_cast<uint8_t>(ch.unicode());
                m_data[m_cursorOffset] = static_cast<char>(newVal);
                m_modifiedOffsets.insert(m_cursorOffset);
                m_modified = true;
                emit dataModified(m_cursorOffset, newVal);
                if (m_cursorOffset + 1 < m_data.size()) {
                    setCursorOffset(m_cursorOffset + 1);
                } else {
                    viewport()->update();
                    emit cursorChanged(m_cursorOffset, true, newVal);
                }
                return;
            }
        } else {
            int nibble = -1;
            const ushort u = ch.toUpper().unicode();
            if (u >= '0' && u <= '9') {
                nibble = u - '0';
            } else if (u >= 'A' && u <= 'F') {
                nibble = 10 + (u - 'A');
            }
            if (nibble >= 0) {
                uint8_t current = static_cast<uint8_t>(m_data[m_cursorOffset]);
                if (!m_lowNibble) {
                    current = static_cast<uint8_t>((nibble << 4) | (current & 0x0F));
                    m_data[m_cursorOffset] = static_cast<char>(current);
                    m_modifiedOffsets.insert(m_cursorOffset);
                    m_modified = true;
                    m_lowNibble = true;
                    emit dataModified(m_cursorOffset, current);
                    emit cursorChanged(m_cursorOffset, true, current);
                    viewport()->update();
                } else {
                    current = static_cast<uint8_t>((current & 0xF0) | nibble);
                    m_data[m_cursorOffset] = static_cast<char>(current);
                    m_modifiedOffsets.insert(m_cursorOffset);
                    m_modified = true;
                    m_lowNibble = false;
                    emit dataModified(m_cursorOffset, current);
                    if (m_cursorOffset + 1 < m_data.size()) {
                        setCursorOffset(m_cursorOffset + 1);
                    } else {
                        emit cursorChanged(m_cursorOffset, true, current);
                        viewport()->update();
                    }
                }
                return;
            }
        }
    }

    QAbstractScrollArea::keyPressEvent(event);
}
