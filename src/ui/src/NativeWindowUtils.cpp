#include <WinJACKNexus/UI/NativeWindowUtils.hpp>

#include <QVariant>
#include <QWindow>
#include <QWidget>

#include <cmath>

namespace WinJACKNexus::UI::NativeWindowUtils {

namespace {

constexpr const char* nativeHandleProperty = "wjnNativeHandle";

QVariant nativeHandleValue(HWND nativeWindow)
{
    return QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(nativeWindow));
}

}

QWidget* embedNativeWindow(HWND nativeWindow, QWidget* parent)
{
    if (nativeWindow == nullptr || !IsWindow(nativeWindow))
        return nullptr;

    auto* window = QWindow::fromWinId(reinterpret_cast<WId>(nativeWindow));
    if (window == nullptr)
        return nullptr;

    auto* container = QWidget::createWindowContainer(window, parent);
    if (container == nullptr) {
        delete window;
        return nullptr;
    }
    container->setProperty(nativeHandleProperty, nativeHandleValue(nativeWindow));
    container->setAttribute(Qt::WA_NativeWindow);
    container->setMinimumSize(1, 1);
    const auto embeddedSize = window->size();
    int minimumHeight = embeddedSize.height();
    RECT clientRect {};
    if (GetClientRect(nativeWindow, &clientRect))
        minimumHeight = std::max(minimumHeight, static_cast<int>(clientRect.bottom - clientRect.top));
    if (minimumHeight > 0)
        container->setMinimumHeight(minimumHeight);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    container->show();
    return container;
}

bool detachNativeWindow(QWidget* container) noexcept
{
    if (container == nullptr)
        return false;
    const auto property = container->property(nativeHandleProperty);
    if (!property.isValid())
        return false;

    const auto nativeValue = property.value<quintptr>();
    auto nativeWindow = reinterpret_cast<HWND>(nativeValue);
    bool detached = true;
    if (nativeWindow != nullptr && IsWindow(nativeWindow)) {
        SetLastError(ERROR_SUCCESS);
        const auto previousParent = SetParent(nativeWindow, nullptr);
        detached = previousParent != nullptr || GetLastError() == ERROR_SUCCESS;
        ShowWindow(nativeWindow, SW_HIDE);
    }
    if (detached)
        container->setProperty(nativeHandleProperty, QVariant {});
    return detached;
}

HWND obtainNativeHandle(const QWidget* widget) noexcept
{
    if (widget == nullptr)
        return nullptr;
    return reinterpret_cast<HWND>(static_cast<quintptr>(widget->winId()));
}

qreal devicePixelRatio(const QWidget* widget) noexcept
{
    return widget != nullptr ? widget->devicePixelRatioF() : 1.0;
}

qreal scaleFactor(HWND nativeWindow) noexcept
{
    if (nativeWindow == nullptr || !IsWindow(nativeWindow))
        return 1.0;
    const auto dpi = GetDpiForWindow(nativeWindow);
    return dpi == 0 ? 1.0 : static_cast<qreal>(dpi) / 96.0;
}

QSize logicalToNativeSize(const QWidget* widget, const QSize& logicalSize) noexcept
{
    const auto scale = devicePixelRatio(widget);
    return QSize(static_cast<int>(std::lround(logicalSize.width() * scale)),
                 static_cast<int>(std::lround(logicalSize.height() * scale)));
}

bool enablePerMonitorDpiAwareness() noexcept
{
    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}

}