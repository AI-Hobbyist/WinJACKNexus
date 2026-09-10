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
#include <WinJACKNexus/UI/MetricReadout.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QImage>
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

bool testCardLifecycleAndCommands()
{
    MockMeterDataProvider provider;
    provider.setPreviewProbeCount(3);
    MeterbridgeWindow window(provider);
    window.resize(700, 760);
    window.show();
    QCoreApplication::processEvents();

    bool passed = true;
    auto* firstCard = window.cardForInstanceId(QStringLiteral("meter-preview-1"));
    const auto* scrollArea = window.findChild<QScrollArea*>(QStringLiteral("meterbridge.cardScroll"));
    passed &= check(window.cardCount() == 3 && firstCard != nullptr && !window.isEmptyStateVisible(),
                    "bridge window creates one card for each preview probe");
    passed &= check(scrollArea != nullptr && scrollArea->horizontalScrollBar()->maximum() > 0,
                    "bridge window preserves fixed card widths through horizontal scrolling");
    const auto* historyAction = firstCard->findChild<QAction*>(QStringLiteral("meterCard.historyAction"));
    const auto* automaticResetAction = firstCard->findChild<QAction*>(QStringLiteral("meterCard.automaticResetAction"));
    passed &= check(historyAction != nullptr && automaticResetAction != nullptr && automaticResetAction->isChecked(),
                    "card exposes history and default-enabled automatic-reset context actions");

    auto* presetSelector = firstCard->findChild<QComboBox*>(QStringLiteral("meterCard.preset"));
    const auto atscIndex = presetSelector != nullptr ? presetSelector->findData(QStringLiteral("atsc-a85")) : -1;
    if (presetSelector != nullptr && atscIndex >= 0)
        presetSelector->setCurrentIndex(atscIndex);
    QCoreApplication::processEvents();
    passed &= check(presetSelector != nullptr && atscIndex >= 0
                        && provider.snapshots().front().presetId == QStringLiteral("atsc-a85")
                        && firstCard->presetId() == QStringLiteral("atsc-a85"),
                    "card preset selection dispatches to its stable preview identity");

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
                        && firstCard->clientName() == QStringLiteral("Program-2"),
                    "client rename updates the existing card instead of rebuilding it");

    provider.setScenario(MockMeterScenario::offline);
    window.refresh();
    QCoreApplication::processEvents();
    const auto* peakReadout = dynamic_cast<WinJACKNexus::UI::MetricReadout*>(
        firstCard->findChild<QWidget*>(QStringLiteral("meterCard.readout.Peak")));
    passed &= check(firstCard->clientName() == QStringLiteral("Offline probe")
                        && peakReadout != nullptr && !peakReadout->value().has_value(),
                    "offline snapshots replace rather than retain stale card measurements");

    QImage image(window.size() * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);
    window.render(&image);
    passed &= check(imageHasVisiblePixels(image), "bridge window renders cards at a high device-pixel ratio");

    provider.setPreviewProbeCount(1);
    window.refresh();
    passed &= check(window.cardCount() == 1 && window.cardForInstanceId(QStringLiteral("meter-preview-1")) == firstCard,
                    "bridge window removes departed cards while preserving surviving card identity");

    provider.setPreviewProbeCount(0);
    window.refresh();
    QCoreApplication::processEvents();
    passed &= check(window.cardCount() == 0 && window.isEmptyStateVisible(),
                    "bridge window presents an empty state when no probes remain");
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