/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <QFileDialog>
#include <QLineEdit>
#include <QSignalBlocker>

namespace WinJACKNexus::UI::ThemeDialogs {
inline QString file(QWidget* parent, bool save, const QString& initial,
                    const char* titleKey, const char* titleEnglish,
                    const char* filterKey, const char* filterEnglish) {
    QFileDialog dialog(parent, {}, initial);
    dialog.setObjectName(QStringLiteral("theme.fileDialog"));
    // Reuse the project's owned Qt dialog workflow so open dialogs can retranslate.
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    bindLocalizedText(&dialog, "windowTitle", "theme", titleKey, titleEnglish);
    const auto refresh = [&dialog, key = QByteArray(filterKey), english = QByteArray(filterEnglish)] {
        auto* edit = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        const auto text = edit ? edit->text() : QString{};
        const auto cursor = edit ? edit->cursorPosition() : 0;
        const auto selection = edit ? edit->selectionStart() : -1;
        const auto length = edit ? edit->selectedText().size() : 0;
        dialog.setNameFilter(localizedText("theme", key.constData(), english.constData()));
        if (edit) {
            const QSignalBlocker blocker(edit);
            edit->setText(text);
            if (selection >= 0) {
                if (cursor == selection) edit->setSelection(selection + length, -length);
                else edit->setSelection(selection, length);
            } else edit->setCursorPosition(cursor);
        }
    };
    if (auto* manager = I18nManager::installed()) manager->bind(&dialog, refresh);
    else refresh();
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString{};
}
}
