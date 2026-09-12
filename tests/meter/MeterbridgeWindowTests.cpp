/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>
#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

#include <cstdio>

namespace {

using WinJACKNexus::Meter::MockMeterDataProvider;
using WinJACKNexus::Meter::MockMeterScenario;
using WinJACKNexus::Meter::Mod::MeterCard;
using WinJACKNexus::Meter::Mod::MeterbridgeWindow;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool imageHasVisiblePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
        }
    }
    return false;
}

bool imageContainsColor(const QImage& image, const QColor& expected)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (QColor::fromRgba(image.pixel(x, y)) == expected)
                return true;
        }
    }
    return false;
}

bool testCardLifecycleAndCommands()
{
    auto& applicationSettings = WinJACKNexus::UI::ApplicationSettings::instance();
    const auto originalFrameRate = applicationSettings.frameRate();
    applicationSettings.setFrameRate(120);
    MockMeterDataProvider provider;
    provider.setPreviewProbeCount(3);
    MeterbridgeWindow window(provider);
    window.resize(700, 760);
    window.show();
    QCoreApplication::processEvents();

    bool passed = true;
    auto* firstCard = window.cardForInstanceId(QStringLiteral("meter-preview-1"));
    auto* secondCard = window.cardForInstanceId(QStringLiteral("meter-preview-2"));
    auto* thirdCard = window.cardForInstanceId(QStringLiteral("meter-preview-3"));
    const auto* scrollArea = window.findChild<QScrollArea*>(QStringLiteral("meterbridge.cardScroll"));
    passed &= check(window.cardCount() == 3 && firstCard != nullptr && !window.isEmptyStateVisible(),
                    "bridge window creates one card for each preview probe");
    passed &= check(window.findChild<QWidget*>(QStringLiteral("meterbridge.previewControls")) == nullptr,
                    "bridge window does not create Mock preview controls");
    const auto persistedState = window.projectState();
    applicationSettings.setFrameRate(30);
    MeterbridgeWindow restoredWindow(provider);
    restoredWindow.restoreProjectState(persistedState);
    const auto* levelBar = firstCard != nullptr
        ? dynamic_cast<const WinJACKNexus::UI::MeterLevelBar*>(
            firstCard->findChild<QWidget*>(QStringLiteral("meterCard.levelBar")))
        : nullptr;
    passed &= check(window.refreshRate() == 30 && window.refreshIntervalMilliseconds() == 33
                        && restoredWindow.refreshRate() == 30 && restoredWindow.refreshIntervalMilliseconds() == 33
                        && levelBar != nullptr && levelBar->frameRate() == 30 && !levelBar->animationRunning()
                        && !persistedState.contains(QStringLiteral("refreshRate")),
                    "application-wide refresh rate drives bridge cards without project-level FPS persistence");
    const auto* peakMetricReadout = firstCard != nullptr
        ? dynamic_cast<const WinJACKNexus::UI::MetricReadout*>(firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Peak")))
        : nullptr;
    const auto* momentaryMetricReadout = firstCard != nullptr
        ? dynamic_cast<const WinJACKNexus::UI::MetricReadout*>(firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Momentary")))
        : nullptr;
    const auto* shortTermMetricReadout = firstCard != nullptr
        ? dynamic_cast<const WinJACKNexus::UI::MetricReadout*>(firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Short-term")))
        : nullptr;
    const auto* integratedMetricReadout = firstCard != nullptr
        ? dynamic_cast<const WinJACKNexus::UI::MetricReadout*>(firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Integrated")))
        : nullptr;
    const auto initialCardHeight = firstCard != nullptr ? firstCard->height() : 0;
    window.resize(480, 900);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    const auto expandedCardHeight = firstCard != nullptr ? firstCard->height() : 0;
    passed &= check(firstCard != nullptr && firstCard->width() == 242 && initialCardHeight >= 560
                        && expandedCardHeight > initialCardHeight
                        && peakMetricReadout != nullptr && peakMetricReadout->label() == QStringLiteral("峰值（Peak）")
                        && momentaryMetricReadout != nullptr && momentaryMetricReadout->label() == QStringLiteral("瞬时（M）")
                        && shortTermMetricReadout != nullptr && shortTermMetricReadout->label() == QStringLiteral("短时（S）")
                        && integratedMetricReadout != nullptr && integratedMetricReadout->label() == QStringLiteral("综合（I）")
                        && !peakMetricReadout->showSurface(),
                    "card preserves compact metric labels while following the available window height");
    passed &= check(scrollArea != nullptr && scrollArea->horizontalScrollBar()->maximum() > 0,
                    "bridge window preserves fixed card widths through horizontal scrolling");
    const auto* firstNumber = firstCard != nullptr ? firstCard->findChild<QLabel*>(QStringLiteral("meterCard.number")) : nullptr;
    const auto* secondNumber = secondCard != nullptr ? secondCard->findChild<QLabel*>(QStringLiteral("meterCard.number")) : nullptr;
    const auto* thirdNumber = thirdCard != nullptr ? thirdCard->findChild<QLabel*>(QStringLiteral("meterCard.number")) : nullptr;
    passed &= check(firstNumber != nullptr && secondNumber != nullptr && thirdNumber != nullptr
                        && firstNumber->text() == QStringLiteral("1") && secondNumber->text() == QStringLiteral("2")
                        && thirdNumber->text() == QStringLiteral("3"),
                    "cards present stable sequential display numbers");

    auto* presetSelector = firstCard->findChild<QComboBox*>(QStringLiteral("meterCard.preset"));
    passed &= check(presetSelector != nullptr
                        && presetSelector->count() == static_cast<int>(WinJACKNexus::Common::meterLoudnessPresetCatalog.size())
                        && presetSelector->findData(QStringLiteral("spotify-default")) >= 0
                        && presetSelector->findData(QStringLiteral("vr-ar-spatial")) >= 0,
                    "card exposes the complete shared loudness-preset catalog");

    window.restoreProjectState({
        { QStringLiteral("cardOrder"), QJsonArray {
            QStringLiteral("meter-preview-3"), QStringLiteral("meter-preview-1"), QStringLiteral("meter-preview-2")
        } },
        { QStringLiteral("selectedMeter"), QStringLiteral("meter-preview-3") },
        { QStringLiteral("currentTab"), 0 }
    });
    QCoreApplication::processEvents();
    passed &= check(thirdNumber->text() == QStringLiteral("1") && firstNumber->text() == QStringLiteral("2")
                        && secondNumber->text() == QStringLiteral("3")
                        && window.projectState().value(QStringLiteral("cardOrder")).toArray().at(0).toString()
                            == QStringLiteral("meter-preview-3"),
                    "restored card order controls both card layout and display numbering");

    const auto* historyAction = firstCard->findChild<QAction*>(QStringLiteral("meterCard.historyAction"));
    const auto* automaticResetAction = firstCard->findChild<QAction*>(QStringLiteral("meterCard.automaticResetAction"));
    passed &= check(historyAction != nullptr && automaticResetAction != nullptr && automaticResetAction->isChecked(),
                    "card exposes history and default-enabled automatic-reset context actions");

    const auto* headerPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.header"));
    const auto* presetPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.presetPanel"));
    const auto* contentPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.content"));
    const auto* levelPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.levelPanel"));
    const auto* metricsPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.metricsPanel"));
    const auto* actionsPanel = firstCard->findChild<QWidget*>(QStringLiteral("meterCard.actions"));
    passed &= check(headerPanel != nullptr && presetPanel != nullptr && contentPanel != nullptr
                        && levelPanel != nullptr && metricsPanel != nullptr && actionsPanel != nullptr
                        && headerPanel->isVisible() && presetPanel->isVisible() && contentPanel->isVisible()
                        && levelPanel->isVisible() && metricsPanel->isVisible() && actionsPanel->isVisible(),
                    "card exposes separate header, preset, metering and action surfaces");

    const auto atscIndex = presetSelector != nullptr ? presetSelector->findData(QStringLiteral("atsc-a85")) : -1;
    if (presetSelector != nullptr && atscIndex >= 0)
        presetSelector->setCurrentIndex(atscIndex);
    QCoreApplication::processEvents();
    passed &= check(presetSelector != nullptr && atscIndex >= 0
                        && provider.snapshots().front().presetId == QStringLiteral("atsc-a85")
                        && firstCard->presetId() == QStringLiteral("atsc-a85"),
                    "card preset selection dispatches to its stable preview identity");

    auto* secondPresetSelector = secondCard != nullptr
        ? secondCard->findChild<QComboBox*>(QStringLiteral("meterCard.preset"))
        : nullptr;
    const auto spotifyIndex = secondPresetSelector != nullptr
        ? secondPresetSelector->findData(QStringLiteral("spotify-default"))
        : -1;
    if (secondPresetSelector != nullptr && spotifyIndex >= 0)
        secondPresetSelector->setCurrentIndex(spotifyIndex);
    QCoreApplication::processEvents();
    const auto presetSnapshots = provider.snapshots();
    passed &= check(secondPresetSelector != nullptr && spotifyIndex >= 0
                        && presetSnapshots.size() >= 2
                        && presetSnapshots[0].presetId == QStringLiteral("atsc-a85")
                        && presetSnapshots[1].presetId == QStringLiteral("spotify-default"),
                    "each card retains its own loudness preset independently");

    auto* recordingButton = firstCard->findChild<QToolButton*>(QStringLiteral("meterCard.recordingButton"));
    if (recordingButton != nullptr)
        recordingButton->click();
    QCoreApplication::processEvents();
    passed &= check(recordingButton != nullptr && provider.snapshots().front().recordingEnabled,
                    "card recording command updates only through the provider contract");

    provider.setScenario(MockMeterScenario::renamed);
    window.refresh();
    QCoreApplication::processEvents();
    passed &= check(window.cardForInstanceId(QStringLiteral("meter-preview-1")) == firstCard
                        && firstCard->clientName() == QStringLiteral("节目-2"),
                    "client rename updates the existing card instead of rebuilding it");

    provider.setScenario(MockMeterScenario::offline);
    window.refresh();
    QCoreApplication::processEvents();
    const auto* peakReadout = dynamic_cast<WinJACKNexus::UI::MetricReadout*>(
        firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Peak")));
    passed &= check(firstCard->clientName() == QStringLiteral("离线探针")
                        && peakReadout != nullptr && !peakReadout->value().has_value(),
                    "offline snapshots replace rather than retain stale card measurements");

    QImage image(window.size() * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);
    window.render(&image);
    passed &= check(imageHasVisiblePixels(image), "bridge window renders cards at a high device-pixel ratio");

    QImage cardImage(firstCard->size() * 2, QImage::Format_ARGB32_Premultiplied);
    cardImage.setDevicePixelRatio(2.0);
    cardImage.fill(Qt::transparent);
    firstCard->render(&cardImage);
    const auto pixelRatio = cardImage.devicePixelRatio();
    const auto cardSurfacePixel = cardImage.pixelColor(qRound(5.0 * pixelRatio),
                                                       qRound(firstCard->height() * pixelRatio / 2.0));
    passed &= check(cardSurfacePixel == QColor(QStringLiteral("#0b1928")),
                    "card paints its root surface through the outer padding");
    passed &= check(imageContainsColor(cardImage, QColor(QStringLiteral("#12263c")))
                        && imageContainsColor(cardImage, QColor(QStringLiteral("#13263b")))
                        && imageContainsColor(cardImage, QColor(QStringLiteral("#ff5269")))
                        && imageContainsColor(cardImage, QColor(QStringLiteral("#43e88d"))),
                    "card renders distinct surfaces and color-coded metric controls");

    provider.setPreviewProbeCount(1);
    window.refresh();
    passed &= check(window.cardCount() == 1 && window.cardForInstanceId(QStringLiteral("meter-preview-1")) == firstCard,
                    "bridge window removes departed cards while preserving surviving card identity");

    provider.setPreviewProbeCount(0);
    window.refresh();
    QCoreApplication::processEvents();
    passed &= check(window.cardCount() == 0 && window.isEmptyStateVisible(),
                    "bridge window presents an empty state when no probes remain");
    applicationSettings.setFrameRate(originalFrameRate);
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (!testCardLifecycleAndCommands())
        return 1;
    std::puts("Meterbridge UI-3 tests passed");
    return 0;
}