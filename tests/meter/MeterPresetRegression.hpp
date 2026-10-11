/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/MeterCustomLoudnessPresets.hpp>
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>
#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUuid>

template <class Check> bool verifyMeterPresets(Check check)
{
    using namespace WinJACKNexus;
    bool passed = true;
    UI::ThemeCore core;
    UI::ThemeManager::installThemeCore(&core);
    Meter::MockMeterDataProvider provider;
    provider.setPreviewProbeCount(1);
    Meter::Mod::MeterCard card(provider, [] {}, [](const QString &) {});
    card.setSnapshot(provider.snapshots().front());
    card.resize(250, 800);
    card.show();
    auto *choice = card.findChild<QComboBox *>(QStringLiteral("meterCard.preset"));
    auto *refresh = card.findChild<QToolButton *>(QStringLiteral("meterCard.presetRefresh"));
    const auto previousId = choice->currentData().toString();
    const int customIndex = choice->findData(QStringLiteral("custom"));
    choice->setCurrentIndex(customIndex);
    passed &= check(!card.findChild<QDialog *>(QStringLiteral("meterCard.customPresetDialog")),
                    "programmatic preset selection does not open the creation dialog");
    bool cancelledCreation = false;
    QTimer::singleShot(0, &card, [&] {
        auto *dialog = card.findChild<QDialog *>(QStringLiteral("meterCard.customPresetDialog"));
        cancelledCreation = dialog && dialog->isVisible();
        if (dialog)
            dialog->reject();
    });
    choice->activated(customIndex);
    passed &= check(
        cancelledCreation && choice->currentData().toString() == previousId,
        "clicking the selected custom entry opens creation and cancel restores the reference");
    const auto name =
        QStringLiteral("响度测试-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QTimer::singleShot(0, &card, [&] {
        auto *dialog = card.findChild<QDialog *>(QStringLiteral("meterCard.customPresetDialog"));
        auto *field =
            dialog ? dialog->findChild<QLineEdit *>(QStringLiteral("lufsPreset.name")) : nullptr;
        auto *target =
            dialog ? dialog->findChild<QDoubleSpinBox *>(QStringLiteral("lufsPreset.target"))
                   : nullptr;
        auto *upper = dialog
                          ? dialog->findChild<QDoubleSpinBox *>(QStringLiteral("lufsPreset.upper"))
                          : nullptr;
        auto *range =
            dialog ? dialog->findChild<QCheckBox *>(QStringLiteral("lufsPreset.range")) : nullptr;
        auto *peak =
            dialog ? dialog->findChild<QDoubleSpinBox *>(QStringLiteral("lufsPreset.truePeak"))
                   : nullptr;
        auto *save =
            dialog ? dialog->findChild<QPushButton *>(QStringLiteral("lufsPreset.save")) : nullptr;
        passed &= check(field && target && upper && range && peak && save,
                        "custom reference editor exposes its values");
        if (!field || !target || !upper || !range || !peak || !save)
        {
            if (dialog)
                dialog->reject();
            return;
        }
        field->setText(name);
        target->setValue(-18);
        range->setChecked(true);
        upper->setValue(-16);
        peak->setValue(-2.5);
        passed &=
            check(field->height() == target->height() && target->height() == upper->height() &&
                      upper->height() == peak->height() && peak->height() == save->height(),
                  "custom preset fields and buttons have equal heights");
        save->click();
    });
    choice->setCurrentIndex(customIndex);
    choice->activated(customIndex);
    const auto id = choice->currentData().toString();
    const auto custom = Common::findCustomMeterLoudnessPreset(id);
    passed &=
        check(custom && custom->displayName == name && custom->targetIntegratedLufs == -18 &&
                  custom->alternateTargetHighLufs == -16 && custom->maximumTruePeakDbtp == -2.5,
              "custom reference saves all values and enters the list");
    const auto path = QCoreApplication::applicationDirPath() + QStringLiteral("/lufs_presets/") +
                      id + QStringLiteral(".json");
    passed &= check(QFile::exists(path), "LUFS presets are independent files beside the program");
    if (refresh)
        refresh->click();
    passed &= check(refresh && choice->height() == refresh->height() &&
                        choice->currentData().toString() == id,
                    "Refresh retains selection and matches the dropdown height");
    const auto invalidPath = QCoreApplication::applicationDirPath() +
                             QStringLiteral("/lufs_presets/bad-%1.json").arg(id);
    QFile invalid(invalidPath);
    passed &= check(invalid.open(QIODevice::WriteOnly), "create malformed LUFS fixture");
    invalid.write("{broken");
    invalid.close();
    Common::refreshCustomMeterLoudnessPresets();
    passed &= check(Common::findCustomMeterLoudnessPreset(id).has_value(),
                    "refresh skips malformed files without losing valid presets");
    QFile::remove(invalidPath);
    choice->showPopup();
    QCoreApplication::processEvents();
    const auto popup = choice->view()->grab().toImage();
    bool hasBackground = false;
    for (int y = 0; y < popup.height() && !hasBackground; ++y)
        for (int x = 0; x < popup.width(); ++x)
            if (popup.pixelColor(x, y) == QColor("#12263c"))
            {
                hasBackground = true;
                break;
            }
    passed &=
        check(hasBackground && choice->view()->palette().color(QPalette::Text).lightnessF() > .7,
              "actual dropdown popup uses dark background and readable light text");
    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
    popup.save(QStringLiteral("tests/preview/PrePush/lufs-preset-popup-modified.png"));
    choice->hidePopup();
    Meter::Mod::MeterHistoryWindow history(provider, [] {}, [] {});
    auto snapshot = provider.snapshots().front();
    snapshot.presetId = id;
    history.setSnapshot(snapshot);
    history.resize(1500, 1000);
    history.show();
    QCoreApplication::processEvents();
    auto *csvDirectory = history.findChild<QLineEdit *>(QStringLiteral("meterSettings.logRoot"));
    if (csvDirectory)
    {
        history.activateWindow();
        csvDirectory->setFocus();
        QCoreApplication::processEvents();
        csvDirectory->setText(QStringLiteral("D:/CSV-edit-in-progress"));
        history.settingsWidget()->setSnapshot(snapshot);
        passed &= check(csvDirectory->text() == QStringLiteral("D:/CSV-edit-in-progress"),
                        "live refresh does not overwrite a CSV directory being edited");
        csvDirectory->setText(snapshot.globalSettings.logRootDirectory);
        csvDirectory->clearFocus();
    }
    const auto &lines = history.chart()->referenceLines();
    passed &= check(lines.size() == 3 && lines[0].value == -18 && lines[1].value == -16 &&
                        lines[2].value == -2.5,
                    "custom targets actually drive chart reference lines");
    QTemporaryDir output;
    QDir().mkpath(output.filePath(QStringLiteral("chosen-folder")));
    const auto png = output.filePath(QStringLiteral("chosen-folder/history.png"));
    QTimer::singleShot(0, &history, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        passed &= check(dialog != nullptr, "PNG export opens a save dialog");
        if (dialog)
        {
            dialog->selectFile(png);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    auto *action = history.findChild<QAction *>(QStringLiteral("meterHistory.exportAction"));
    if (action)
        action->trigger();
    const QImage image(png);
    passed &= check(!image.isNull() && image.width() >= 100 && image.height() >= 100,
                    "Export writes a readable PNG of the chart");
    const auto *status = history.findChild<QLabel *>(QStringLiteral("meterHistory.exportStatus"));
    passed &= check(status && status->text().contains(png), "PNG export reports the saved path");
    passed &= check(status && status->palette().color(QPalette::WindowText).lightnessF() > .7,
                    "history sidebar status text remains readable on its themed surface");
    const auto historyImage = history.grab().toImage();
    const auto scale = historyImage.devicePixelRatio();
    passed &=
        check(historyImage.pixelColor(qRound(20 * scale), qRound(980 * scale)) == QColor("#061523"),
              "history sidebar background uses the theme rather than the system white surface");
    historyImage.save(
        QStringLiteral("tests/preview/PrePush/meter-presets-and-export-modified.png"));
    QTimer::singleShot(0, &history, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    if (action)
        action->trigger();
    passed &= check(
        QDir(output.filePath(QStringLiteral("chosen-folder"))).entryList(QDir::Files).size() == 1,
        "canceling export creates no extra files");
    QFile::remove(path);
    Common::refreshCustomMeterLoudnessPresets();
    UI::ThemeManager::installThemeCore(nullptr);
    return passed;
}
