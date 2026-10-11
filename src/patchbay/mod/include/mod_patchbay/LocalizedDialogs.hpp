/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QEvent>

namespace PatchbayDialogs
{
inline void bind(QObject *object, const char *property, const char *key, const char *english,
                 const QVariantMap &arguments = {})
{
    WinJACKNexus::UI::bindLocalizedText(object, property, "patchbay", key, english, arguments);
}
// Qt's LanguageChange handler also rewrites the input dialog's internal label.
// Restore the application prompt after Qt has refreshed its standard buttons.
class InputLabelRefresh final : public QObject
{
  public:
    InputLabelRefresh(QInputDialog &dialog, const char *key, const char *english)
        : QObject(&dialog), dialog_(dialog), key_(key), english_(english)
    {
        dialog.installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == &dialog_ && event->type() == QEvent::LanguageChange)
            QTimer::singleShot(0, this, [this] {
                dialog_.setLabelText(WinJACKNexus::UI::localizedText("patchbay", key_.constData(),
                                                                     english_.constData()));
            });
        return QObject::eventFilter(object, event);
    }

  private:
    QInputDialog &dialog_;
    QByteArray key_, english_;
};
inline QMessageBox::StandardButton message(
    QWidget *parent, QMessageBox::Icon icon, const char *titleKey, const char *titleEnglish,
    const char *bodyKey, const char *bodyEnglish,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
    const QVariantMap &arguments = {})
{
    QMessageBox dialog(icon, {}, {}, buttons, parent);
    bind(&dialog, "windowTitle", titleKey, titleEnglish);
    bind(&dialog, "text", bodyKey, bodyEnglish, arguments);
    dialog.setDefaultButton(defaultButton);
    return static_cast<QMessageBox::StandardButton>(dialog.exec());
}
inline QString text(QWidget *parent, const char *titleKey, const char *titleEnglish,
                    const char *labelKey, const char *labelEnglish, const QString &initial,
                    bool *accepted)
{
    QInputDialog dialog(parent);
    new InputLabelRefresh(dialog, labelKey, labelEnglish);
    bind(&dialog, "windowTitle", titleKey, titleEnglish);
    bind(&dialog, "labelText", labelKey, labelEnglish);
    dialog.setTextValue(initial);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted)
        *accepted = ok;
    return ok ? dialog.textValue() : QString{};
}
inline int integer(QWidget *parent, const char *titleKey, const char *titleEnglish,
                   const char *labelKey, const char *labelEnglish, int initial, int minimum,
                   int maximum, int step, bool *accepted)
{
    QInputDialog dialog(parent);
    dialog.setInputMode(QInputDialog::IntInput);
    new InputLabelRefresh(dialog, labelKey, labelEnglish);
    bind(&dialog, "windowTitle", titleKey, titleEnglish);
    bind(&dialog, "labelText", labelKey, labelEnglish);
    dialog.setIntRange(minimum, maximum);
    dialog.setIntStep(step);
    dialog.setIntValue(initial);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted)
        *accepted = ok;
    return dialog.intValue();
}
inline QString projectFile(QWidget *parent, bool save, const QString &initial)
{
    QFileDialog dialog(parent, {}, initial);
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    bind(&dialog, "windowTitle", save ? "dialog.saveProject" : "dialog.openProject",
         save ? "Save patchbay project" : "Open patchbay project");
    const auto updateFilter = [&dialog] {
        auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        const auto text = edit ? edit->text() : QString{};
        const auto cursor = edit ? edit->cursorPosition() : 0;
        const auto selection = edit ? edit->selectionStart() : -1;
        const auto length = edit ? edit->selectedText().size() : 0;
        dialog.setNameFilter(WinJACKNexus::UI::localizedText("patchbay", "project.fileFilter",
                                                             "WinJACKNexus project (*.wjnproj)"));
        if (edit)
        {
            const QSignalBlocker blocked(edit);
            edit->setText(text);
            if (selection >= 0)
                edit->setSelection(selection, length);
            else
                edit->setCursorPosition(cursor);
        }
    };
    if (auto *manager = WinJACKNexus::UI::I18nManager::installed())
        manager->bind(&dialog, updateFilter);
    else
        updateFilter();
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString{};
}
}
