/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Editor.hpp"
#include <QGraphicsOpacityEffect>
#include <QSignalBlocker>
#include <WinJACKNexus/UI/LocalizedText.hpp>

namespace WinJACKNexus::SignalSwitch
{
namespace
{
Editor *hookOwner{};
}

void Editor::setLocked(bool locked, bool capturePosition)
{
    if (locked && capturePosition && isVisible())
    {
        rememberGeometry();
        service_.state.window.overlayOrigin = canvas_->mapToGlobal(QPoint());
    }
    service_.state.locked = locked;
    selected_.clear();
    dragging_ = selecting_ = false;
    rubber_->hide();
    refresh();
    if (!locked && capturePosition)
        show();
    service_.notify(true);
}

void Editor::styleButton(UI::Controls::ActionButton *button, const QString &id)
{
    using namespace UI::Controls;
    const auto scoped = [&](const char *role, const QColor &fallback) {
        return themeTokenColor("signal-switch", role, themeTokenColor("global", role, fallback));
    };
    const auto native = button->palette();
    const auto card = service_.state.cards.value(id);
    button->setProperty("surfaceColor", card.color.isValid()
                                            ? card.color
                                            : scoped("control-button-surface-normal", native.color(QPalette::Button)));
    button->setProperty("textColor", scoped("control-button-text-normal", native.color(QPalette::ButtonText)));
    button->setProperty("accentColor", scoped("control-button-border-selected", native.color(QPalette::Highlight)));
    button->setProperty("borderColor", selected_.contains(id)
                                           ? scoped("accent-primary", native.color(QPalette::Highlight))
                                           : scoped("control-button-border-normal", native.color(QPalette::Mid)));
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(button->graphicsEffect());
    if (!effect)
    {
        effect = new QGraphicsOpacityEffect(button);
        button->setGraphicsEffect(effect);
    }
    effect->setOpacity(service_.state.window.opacity / 100.0);
}

void Editor::refreshPresentation()
{
    if (!lockAction_)
        return;
    const bool locked = service_.state.locked;
    lockAction_->setEnabled(!locked);
    unlockAction_->setEnabled(locked);
    throughAction_->setEnabled(locked);
    {
        const QSignalBlocker blocked(throughAction_);
        throughAction_->setChecked(service_.state.window.clickThrough);
    }
    if (locked)
    {
        hide();
        for (auto it = buttons_.begin(); it != buttons_.end(); ++it)
        {
            auto *overlay = overlays_.value(it.key());
            if (!overlay)
            {
                overlay = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                                   Qt::WindowDoesNotAcceptFocus);
                overlay->setObjectName("signalSwitchOverlay." + it.key());
                overlay->setAttribute(Qt::WA_TranslucentBackground);
                overlay->setAttribute(Qt::WA_ShowWithoutActivating);
                overlays_[it.key()] = overlay;
                it.value()->setParent(overlay);
            }
            const auto card = service_.state.cards.value(it.key()).geometry;
            overlay->setGeometry(QRect(service_.state.window.overlayOrigin + card.topLeft(), card.size()));
            it.value()->setGeometry(QRect(QPoint(), card.size()));
            it.value()->show();
            if (!overlay->isVisible())
                overlay->show();
        }
    }
    else if (!overlays_.isEmpty())
    {
        for (auto it = buttons_.begin(); it != buttons_.end(); ++it)
        {
            it.value()->setParent(canvas_);
            it.value()->setGeometry(service_.state.cards.value(it.key()).geometry);
            it.value()->show();
        }
        qDeleteAll(overlays_);
        overlays_.clear();
    }
    updateMouseHook();
    for (auto *overlay : overlays_)
    {
        const auto handle = reinterpret_cast<HWND>(overlay->winId());
        const auto oldStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
        const auto style =
            service_.state.window.clickThrough ? oldStyle | WS_EX_TRANSPARENT : oldStyle & ~LONG_PTR(WS_EX_TRANSPARENT);
        if (style != oldStyle)
            SetWindowLongPtrW(handle, GWL_EXSTYLE, style);
    }
}

void Editor::releaseMouseHook()
{
    if (mouseHook_)
        UnhookWindowsHookEx(static_cast<HHOOK>(mouseHook_));
    mouseHook_ = nullptr;
    hookPressed_.clear();
    if (hookOwner == this)
        hookOwner = nullptr;
}

void Editor::updateMouseHook()
{
    if (!service_.state.locked || !service_.state.window.clickThrough)
    {
        releaseMouseHook();
        return;
    }
    if (mouseHook_)
        return;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Editor::mouseHook), &module);
    hookOwner = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &Editor::mouseHook, module, 0);
    if (!mouseHook_)
    {
        hookOwner = nullptr;
        service_.state.window.clickThrough = false;
        throughAction_->setChecked(false);
        status_->setText(UI::localizedText("signalSwitch", "hookFailure",
                                           "Mouse click-through could not be enabled. Buttons remain clickable."));
        status_->show();
    }
}

LRESULT CALLBACK Editor::mouseHook(int code, WPARAM message, LPARAM data)
{
    auto *owner = hookOwner;
    if (code < 0 || !owner || (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP))
        return CallNextHookEx(nullptr, code, message, data);
    const auto &event = *reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
    QString hit;
    // Resolve overlapping buttons in their actual Windows z-order.
    for (HWND handle = GetTopWindow(nullptr); handle && hit.isEmpty(); handle = GetWindow(handle, GW_HWNDNEXT))
    {
        for (auto it = owner->overlays_.constBegin(); it != owner->overlays_.constEnd(); ++it)
        {
            if (reinterpret_cast<HWND>(it.value()->winId()) != handle)
                continue;
            RECT rect{};
            if (IsWindowVisible(handle) && GetWindowRect(handle, &rect) && PtInRect(&rect, event.pt))
                hit = it.key();
            break;
        }
    }
    if (message == WM_LBUTTONDOWN && (GetAsyncKeyState(VK_MENU) & 0x8000) && !hit.isEmpty())
    {
        owner->hookPressed_ = hit;
        return 1;
    }
    if (message == WM_LBUTTONUP && !owner->hookPressed_.isEmpty())
    {
        const auto id = owner->hookPressed_;
        owner->hookPressed_.clear();
        if (hit == id)
        {
            QMetaObject::invokeMethod(owner, [owner, id] { owner->service_.activate(id); }, Qt::QueuedConnection);
        }
        return 1;
    }
    return CallNextHookEx(nullptr, code, message, data);
}
} // namespace WinJACKNexus::SignalSwitch
