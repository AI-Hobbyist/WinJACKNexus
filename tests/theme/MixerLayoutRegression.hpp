/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <QApplication>
#include <QDir>
#include <QMouseEvent>

template<class Check>
void verifyMixerLayout(QApplication& app, Check check)
{
    using namespace WinJACKNexus;
    UI::ThemeCore core;
    for (const auto* kind : {"input", "aux", "master"})
        check(!core.snapshot()->hasValue(QStringLiteral("--wjn-theme-mixer-group-%1-accent").arg(kind)),
              "removed mixer card tokens are absent from the registry and default theme");
    for (const auto* role : {"monitor-surface", "monitor-border"})
        check(!core.snapshot()->hasValue(QStringLiteral("--wjn-theme-mixer-%1").arg(role)),
              "removed monitor card tokens are absent");
    for (const auto* role : {"resize-handle", "category-divider"})
        check(core.snapshot()->hasValue(QStringLiteral("--wjn-theme-mixer-%1").arg(role)),
              "new mixer separator tokens are registered");
    UI::ThemeManager::installThemeCore(&core);
    UI::ThemeManager manager;
    manager.watchApplication(&app);
    Common::DigitalMixerState state;
    const auto first = state.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    state.strip(first)->name = QStringLiteral("声卡输入");
    state.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    state.add(Common::MixerStripKind::aux, Common::MixerLayout::surround71);
    UI::MixerWindow mixer(&state, [&state](const auto& next, QString*) { state = next; return true; });
    mixer.resize(1024, 768);
    mixer.show();
    app.processEvents();
    const QColor divider("#ff00ff");
    const QColor fixedDivider("#00ffff");
    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-resize-handle"), divider.name()),
          "resize handle token styles the draggable boundary");
    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-category-divider"), fixedDivider.name()),
          "category divider token independently styles the fixed boundary");
    const auto image = [&] { app.processEvents(); return mixer.grab().toImage(); };
    const auto boundary = [&](int x, const QColor& expected) {
        const auto pixels = image();
        const qreal scale = pixels.devicePixelRatio();
        return pixels.pixelColor(qRound(x * scale), qRound(200 * scale)) == expected
            && pixels.pixelColor(qRound(x * scale), qRound(700 * scale)) == expected;
    };
    check(boundary(477, divider) && boundary(795, fixedDivider), "Input/AUX and AUX/Listen boundaries span the channel area");
    const auto mouse = [&](QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(point), QPointF(mixer.mapToGlobal(point)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&mixer, &event);
    };
    const auto drag = [&](int from, int to) {
        mouse(QEvent::MouseButtonPress, {from, 200}, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, {to, 200}, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, {to, 200}, Qt::LeftButton, Qt::NoButton);
    };
    drag(477, 360);
    check(boundary(360, divider) && boundary(795, fixedDivider), "dragging left shrinks Input and grows AUX without moving Listen/Master");
    drag(360, 600);
    check(boundary(600, divider) && boundary(795, fixedDivider), "dragging right grows Input and shrinks AUX");
    mouse(QEvent::MouseButtonPress, {35, 414}, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, {35, 414}, Qt::LeftButton, Qt::NoButton);
    check(state.strip(first)->listenMode == Common::MixerListenMode::pfl,
          "PFL hit area follows the reclaimed header space");
    check(core.removeOverride(QStringLiteral("--wjn-theme-mixer-resize-handle")), "restore divider color");
    check(core.removeOverride(QStringLiteral("--wjn-theme-mixer-category-divider")), "restore fixed divider color");
    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
    check(image().save(QStringLiteral("tests/preview/PrePush/mixer-without-group-cards-modified.png")),
          "capture the mixer without group cards for human review");
    UI::ThemeManager::installThemeCore(nullptr);
}
