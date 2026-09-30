/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QUuid>

template<class Check>
void verifyFxChainPresets(QApplication& app, Check check)
{
    using namespace WinJACKNexus;
    UI::ThemeCore core;
    UI::ThemeManager::installThemeCore(&core);
    const auto unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto name = QStringLiteral("预设回归-%1").arg(unique);
    UI::FxSettingsWindow fx(QStringLiteral("Preset regression"), {QStringLiteral("L"), QStringLiteral("R")});
    auto* choice = fx.findChild<QComboBox*>(QStringLiteral("fxWindowPreset"));
    auto* save = fx.findChild<QPushButton*>(QStringLiteral("fxChainPresetSave"));
    auto* saveAs = fx.findChild<QPushButton*>(QStringLiteral("fxChainPresetSaveAs"));
    auto* refresh = fx.findChild<QPushButton*>(QStringLiteral("fxChainPresetRefresh"));
    check(choice && save && saveAs && refresh, "chain preset controls exist");
    if (!choice || !save || !saveAs || !refresh) { UI::ThemeManager::installThemeCore(nullptr); return; }
    std::vector<Common::MixerEffectState> captured;
    bool capturedEnabled = false;
    fx.setChainCommitHandler([&](const auto& chain, bool enabled) {
        captured = chain; capturedEnabled = enabled; return true;
    });
    fx.setChainEnabled(true);
    check(!captured.empty() && captured.front().parameters.contains(QStringLiteral("snapshot")),
          "exercise an actual Native Fx parameter snapshot");
    if (captured.empty()) { UI::ThemeManager::installThemeCore(nullptr); return; }
    const auto native = captured.front();
    auto disabled = native;
    disabled.enabled = false;
    Common::MixerEffectState missing;
    missing.moduleId = QStringLiteral("uninstalled-regression-module");
    missing.enabled = false;
    missing.parameters = QJsonObject {{"custom", QStringLiteral("保留参数")}};
    const std::vector<Common::MixerEffectState> expected {disabled, missing, native};
    fx.loadChain(expected, true);
    fx.show(); app.processEvents();
    const auto fillName = [&] {
        QTimer::singleShot(0, &fx, [&] {
            auto* dialog = fx.findChild<QDialog*>(QStringLiteral("fxChainPresetNameDialog"));
            auto* field = dialog ? dialog->findChild<QLineEdit*>(QStringLiteral("fxChainPresetName")) : nullptr;
            auto* accept = dialog ? dialog->findChild<QPushButton*>(QStringLiteral("fxChainPresetNameSave")) : nullptr;
            check(field && accept, "Save As asks for a name using shared controls");
            if (field && accept) { field->setText(name); accept->click(); }
            else if (dialog) dialog->reject();
        });
    };
    const int initialCount = choice->count();
    fillName(); save->click();
    check(choice->count() == initialCount + 1 && choice->currentText() == name,
          "Save without a selection creates and selects an independent preset");
    const auto path = choice->currentData().toString();
    check(QFileInfo(path).absolutePath() == QCoreApplication::applicationDirPath() + QStringLiteral("/fx_chain_presets"),
          "presets live beside the application in fx_chain_presets");
    const auto read = [&] {
        QFile file(path); check(file.open(QIODevice::ReadOnly), "open saved preset"); return QJsonDocument::fromJson(file.readAll()).object();
    };
    auto saved = read();
    check(saved.value("enabled").toBool() && saved.value("effects").toArray().size() == 3
          && saved.value("effects").toArray()[0].toObject().value("parameters").toObject() == native.parameters
          && saved.value("effects").toArray()[1].toObject().value("parameters").toObject() == missing.parameters,
          "saved file retains ordered modules, snapshots, power and missing module parameters");
    fx.loadChain({}, false); save->click();
    saved = read();
    check(choice->count() == initialCount + 1 && !saved.value("enabled").toBool()
          && saved.value("effects").toArray().isEmpty(), "Save atomically overwrites the selected preset");
    fx.loadChain(expected, true); save->click();
    fillName(); saveAs->click();
    const auto secondPath = choice->currentData().toString();
    check(secondPath != path && choice->count() == initialCount + 2, "Save As creates a separate file");
    const auto externalPath = QFileInfo(path).absolutePath() + QStringLiteral("/external-%1.json").arg(unique);
    QFile external(externalPath);
    saved = read(); saved.insert("name", name + QStringLiteral("-外部"));
    check(external.open(QIODevice::WriteOnly), "write an externally added preset");
    external.write(QJsonDocument(saved).toJson()); external.close();
    fx.loadChain({}, false);
    captured.clear();
    refresh->click();
    check(choice->findData(externalPath) >= 0 && captured.empty(), "Refresh discovers external files without loading a chain");
    const int index = choice->findData(externalPath);
    choice->setCurrentIndex(index);
    QMetaObject::invokeMethod(choice, "activated", Qt::DirectConnection, Q_ARG(int, index));
    check(capturedEnabled && captured.size() == 3 && captured[0].moduleId == native.moduleId
          && !captured[0].enabled && captured[1].parameters == missing.parameters
          && captured[2].parameters == native.parameters, "selecting a preset commits its complete chain to the channel");
    save->click();
    QFile roundtrip(externalPath); check(roundtrip.open(QIODevice::ReadOnly), "open roundtrip preset");
    const auto restored = QJsonDocument::fromJson(roundtrip.readAll()).object(); roundtrip.close();
    check(restored.value("effects") == saved.value("effects"), "loaded Native Fx and missing modules survive save roundtrip");
    fx.setChainCommitHandler([](const auto&, bool) { return false; });
    choice->setCurrentIndex(choice->findData(path));
    QMetaObject::invokeMethod(choice, "activated", Qt::DirectConnection, Q_ARG(int, choice->currentIndex()));
    check(choice->currentData().toString() == externalPath, "rejected chain commit restores preset selection");
    const auto badPath = QFileInfo(path).absolutePath() + QStringLiteral("/bad-%1.json").arg(unique);
    QFile bad(badPath); check(bad.open(QIODevice::WriteOnly), "create malformed fixture"); bad.write("{broken"); bad.close();
    refresh->click();
    check(choice->findData(badPath) < 0, "Refresh skips malformed files");
    {
        UI::FxSettingsWindow reopened(QStringLiteral("Reopened"), {QStringLiteral("L"), QStringLiteral("R")});
        auto* persisted = reopened.findChild<QComboBox*>(QStringLiteral("fxWindowPreset"));
        check(persisted && persisted->findData(path) >= 0 && persisted->currentIndex() == 0,
              "new windows discover disk presets without automatically applying one");
    }
    check(core.setOverride(QStringLiteral("--wjn-theme-sel-fx-control-preset-button-surface"), QStringLiteral("#ff00ff")),
          "preset action theme token is registered");
    app.processEvents();
    check(fx.styleSheet().contains(QStringLiteral("background:#ff00ff")), "preset action stylesheet consumes the token");
    core.removeOverride(QStringLiteral("--wjn-theme-sel-fx-control-preset-button-surface"));
    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
    fx.grab().save(QStringLiteral("tests/preview/PrePush/fx-chain-presets-modified.png"));
    for (const auto& file : {path, secondPath, externalPath, badPath}) QFile::remove(file);
    UI::ThemeManager::installThemeCore(nullptr);
}
