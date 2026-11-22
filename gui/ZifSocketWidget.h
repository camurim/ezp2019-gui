#pragma once

#include "ChipDatabase.h"
#include "ZifSvgGenerator.h"

#include <QByteArray>
#include <QSvgRenderer>
#include <QWidget>

#include <optional>

class ZifSocketWidget : public QWidget {
    Q_OBJECT

public:
    explicit ZifSocketWidget(QWidget *parent = nullptr);

    void setChip(const std::optional<ChipInfo> &chip);
    void setPackage(ChipPackage package);
    ChipPackage package() const { return m_package; }

    const QByteArray &svgMarkup() const { return m_svgData; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void rebuildSvg();

    std::optional<ChipInfo> m_chip;
    ChipPackage m_package = ChipPackage::Dip8;
    QByteArray m_svgData;
    QSvgRenderer m_renderer;
};
