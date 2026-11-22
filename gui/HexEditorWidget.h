#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QSet>

#include <cstdint>

class HexEditorWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit HexEditorWidget(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    void setData(QByteArray &&data);
    const QByteArray &data() const { return m_data; }

    void clear();
    void fill(uint8_t value, qsizetype size);
    void fillExisting(uint8_t value);

    bool isModified() const { return m_modified; }
    qsizetype modifiedCount() const { return m_modifiedOffsets.size(); }
    void setModified(bool modified);

    qsizetype cursorOffset() const { return m_cursorOffset; }

    // Search & match highlight
    qsizetype findNext(const QByteArray &pattern);
    qsizetype findPrevious(const QByteArray &pattern);
    void clearHighlight();

signals:
    void cursorChanged(qsizetype offset, bool hasData, uint8_t byteValue);
    void dataModified(qsizetype offset, uint8_t newValue);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr int kBytesPerRow = 16;

    struct LayoutMetrics {
        int charWidth = 8;
        int rowHeight = 18;
        int headerHeight = 24;
        int addrX = 8;
        int hexX = 96;
        int asciiX = 480;
        int totalWidth = 640;
    };

    void updateMetrics();
    void updateScrollBars();
    void ensureCursorVisible();
    void setCursorOffset(qsizetype offset);
    qsizetype rowCount() const;
    int byteHexX(int col) const;

    QByteArray m_data;
    QSet<qsizetype> m_modifiedOffsets;
    LayoutMetrics m_metrics;
    qsizetype m_cursorOffset = 0;
    qsizetype m_highlightOffset = -1;
    qsizetype m_highlightLength = 0;
    bool m_inAsciiPane = false;
    bool m_lowNibble = false;
    bool m_modified = false;
};
