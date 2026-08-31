#include <WinJACKNexus/UI/PaintableWidget.hpp>

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>

namespace WinJACKNexus::UI {

PaintableWidget::PaintableWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setProperty("wjnRole", QString {});
}

PaintableWidget::~PaintableWidget() = default;

bool PaintableWidget::renderingActive() const noexcept
{
    return renderingActive_;
}

bool PaintableWidget::isRenderingActive() const noexcept
{
    return renderingActive();
}

void PaintableWidget::setRenderingActive(bool active)
{
    if (renderingActive_ == active)
        return;
    renderingActive_ = active;
    renderingStateChanged();
    update();
    emit renderingActiveChanged(renderingActive_);
}

QString PaintableWidget::wjnRole() const
{
    return wjnRole_;
}

void PaintableWidget::setWjnRole(const QString& role)
{
    if (wjnRole_ == role)
        return;
    wjnRole_ = role;
    setProperty("wjnRole", wjnRole_);
    if (style() != nullptr) {
        style()->unpolish(this);
        style()->polish(this);
    }
    update();
}

void PaintableWidget::requestRepaint()
{
    update();
    emit repaintRequested();
}

void PaintableWidget::paintEvent(QPaintEvent* event)
{
    if (!isRenderingAllowed()) {
        event->accept();
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintContent(painter);
}

void PaintableWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    renderingStateChanged();
}

void PaintableWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    renderingStateChanged();
}

void PaintableWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        renderingStateChanged();
}

bool PaintableWidget::isRenderingAllowed() const noexcept
{
    return renderingActive_ && isVisible() && !isMinimized();
}

void PaintableWidget::renderingStateChanged() noexcept
{
}

}