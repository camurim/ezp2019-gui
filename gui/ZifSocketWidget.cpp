#include "ZifSocketWidget.h"

#include <QEvent>
#include <QPainter>

ZifSocketWidget::ZifSocketWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rebuildSvg();
}

void ZifSocketWidget::setChip(const std::optional<ChipInfo> &chip)
{
    m_chip = chip;
    rebuildSvg();
}

void ZifSocketWidget::setPackage(ChipPackage package)
{
    if (m_package == package) {
        return;
    }
    m_package = package;
    rebuildSvg();
}

QSize ZifSocketWidget::sizeHint() const
{
    return {260, 268};
}

QSize ZifSocketWidget::minimumSizeHint() const
{
    return {210, 220};
}

void ZifSocketWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        rebuildSvg();
    }
}

void ZifSocketWidget::rebuildSvg()
{
    const QPalette &pal = palette();
    ZifSvgTheme theme;
    theme.cardBg = pal.base().color();
    theme.cardBorder = pal.mid().color();
    theme.primaryText = pal.text().color();
    theme.mutedText = pal.color(QPalette::PlaceholderText);

    m_svgData = ZifSvgGenerator::generate(m_chip, m_package, theme);
    m_renderer.load(m_svgData);
    update();
}

void ZifSocketWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_renderer.isValid()) {
        return;
    }

    const QSizeF svgSize = m_renderer.defaultSize();
    const QRectF avail = rect().adjusted(2, 2, -2, -2);
    if (svgSize.isEmpty() || avail.isEmpty()) {
        return;
    }

    const qreal scale = qMin(avail.width() / svgSize.width(), avail.height() / svgSize.height());
    const QSizeF targetSize(svgSize.width() * scale, svgSize.height() * scale);
    const QPointF topLeft(avail.center().x() - targetSize.width() / 2.0,
                          avail.center().y() - targetSize.height() / 2.0);

    m_renderer.render(&p, QRectF(topLeft, targetSize));
}
