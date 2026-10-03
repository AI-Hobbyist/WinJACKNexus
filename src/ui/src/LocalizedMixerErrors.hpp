/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <QMessageBox>

namespace WinJACKNexus::UI::MixerErrors {
// The existing model API returns canonical diagnostics. Adapt only the display
// copy at the GUI boundary; audio processing, persisted state and logs retain it.
inline QString display(const QString& canonical) {
    if (canonical == QStringLiteral("JACK 端口注册失败，旧拓扑已保留。")) return localizedText("mixer", "error.registerPorts", "Unable to register mixer ports. The previous state has been retained.");
    if (canonical == QStringLiteral("JACK 端口映射失败。")) return localizedText("mixer", "error.mapPorts", "Unable to map JACK ports.");
    if (canonical == QStringLiteral("AUX 路由存在环")) return localizedText("mixer", "error.auxRouteCycle", "AUX routing contains a cycle");
    if (canonical == QStringLiteral("无效的 FX 采样率。")) return localizedText("mixer", "error.fxSampleRate", "Invalid FX sample rate.");
    if (canonical == QStringLiteral("无效的 Sub Mix 路由")) return localizedText("mixer", "error.subRoute", "Invalid Sub Mix routing");
    if (canonical == QStringLiteral("无效或自发 AUX 路由")) return localizedText("mixer", "error.selfAuxRoute", "Invalid or self-referencing AUX routing");
    if (canonical == QStringLiteral("AUX 声像参数无效")) return localizedText("mixer", "error.auxPan", "Invalid AUX pan parameters");
    if (canonical == QStringLiteral("MAIN 必须是不可删除的立体声总线")) return localizedText("mixer", "error.mainBus", "MAIN must be a permanent stereo bus");
    if (canonical == QStringLiteral("监听推子参数无效")) return localizedText("mixer", "error.monitorParameters", "Invalid monitor fader parameters");
    if (canonical == QStringLiteral("MAIN 参数无效")) return localizedText("mixer", "error.mainParameters", "Invalid MAIN parameters");
    if (canonical == QStringLiteral("CH 参数或身份无效")) return localizedText("mixer", "error.inputParameters", "Invalid CH parameters or identity");
    if (canonical == QStringLiteral("AUX 参数或身份无效")) return localizedText("mixer", "error.auxParameters", "Invalid AUX parameters or identity");
    if (canonical == QStringLiteral("Sub Mix 参数或身份无效")) return localizedText("mixer", "error.subParameters", "Invalid Sub Mix parameters or identity");
    if (canonical == QStringLiteral("通道名生成了无效或重复的 JACK 端口名")) return localizedText("mixer", "error.portName", "The channel name produces an invalid or duplicate JACK port name");
    if (canonical == QStringLiteral("AUX 路由无效或重复")) return localizedText("mixer", "error.duplicateRoute", "Invalid or duplicate AUX routing");
    if (canonical == QStringLiteral("总线路由存在环")) return localizedText("mixer", "error.routeCycle", "Bus routing contains a cycle");
    if (canonical == QStringLiteral("FX 链拓扑已变更。")) return localizedText("mixer", "error.effectTopology", "The FX chain topology has changed.");
    if (canonical.endsWith(QStringLiteral(" 缺少逐声道参数快照。请在 FX 设置中重新添加。")))
        return localizedText("mixer", "error.missingSnapshot", "%1 has no per-channel parameter snapshot. Add it again in FX settings.").arg(canonical.left(canonical.size() - QStringLiteral(" 缺少逐声道参数快照。请在 FX 设置中重新添加。").size()));
    if (canonical.endsWith(QStringLiteral(" 的参数快照 Base64 格式无效。")))
        return localizedText("mixer", "error.snapshotBase64", "Invalid Base64 parameter snapshot for %1.").arg(canonical.left(canonical.size() - QStringLiteral(" 的参数快照 Base64 格式无效。").size()));
    if (canonical.endsWith(QStringLiteral(" 的参数快照长度无效。")))
        return localizedText("mixer", "error.snapshotSize", "Invalid parameter snapshot size for %1.").arg(canonical.left(canonical.size() - QStringLiteral(" 的参数快照长度无效。").size()));
    if (canonical.endsWith(QStringLiteral(" 的参数快照版本或模块身份不匹配。")))
        return localizedText("mixer", "error.snapshotIdentity", "Parameter snapshot version or module identity mismatch for %1.").arg(canonical.left(canonical.size() - QStringLiteral(" 的参数快照版本或模块身份不匹配。").size()));
    if (canonical.endsWith(QStringLiteral(" 的参数快照导入失败。")))
        return localizedText("mixer", "error.snapshotImport", "Parameter snapshot import failed for %1.").arg(canonical.left(canonical.size() - QStringLiteral(" 的参数快照导入失败。").size()));
    return canonical;
}
inline void warning(QWidget* parent, const char* titleKey, const char* titleEnglish, const QString& canonical,
                    const char* fallbackKey = nullptr, const char* fallbackEnglish = nullptr) {
    QMessageBox dialog(QMessageBox::Warning, {}, {}, QMessageBox::Ok, parent);
    dialog.setTextFormat(Qt::PlainText);
    bindLocalizedText(&dialog, "windowTitle", "mixer", titleKey, titleEnglish);
    const QByteArray key(fallbackKey ? fallbackKey : ""), english(fallbackEnglish ? fallbackEnglish : "");
    const auto refresh = [&dialog, canonical, key, english] {
        dialog.setText(canonical.isEmpty() && !key.isEmpty()
            ? localizedText("mixer", key.constData(), english.constData()) : display(canonical));
    };
    if (auto* manager = I18nManager::installed()) manager->bind(&dialog, refresh); else refresh();
    dialog.exec();
}
}
