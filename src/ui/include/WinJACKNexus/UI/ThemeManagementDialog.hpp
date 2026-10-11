/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/UI/ThemePackage.hpp>

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <memory>

class QCloseEvent;

namespace WinJACKNexus::UI
{

class ThemeCore;

// Authoring and package management window built from the shared UI controls.
class WINJACK_UI_API ThemeManagementDialog final : public QDialog
{
  public:
    explicit ThemeManagementDialog(ThemeCore &core, QWidget *parent = nullptr);
    ~ThemeManagementDialog() override;

    bool openPackage(const QString &path, QString *error = nullptr);
    bool preview(QString *error = nullptr);
    bool apply(QString *error = nullptr);
    bool cancelPreview();
    bool exportToFile(const QString &path, const QString &namespacePrefix = {},
                      bool builtInDefault = false, QString *error = nullptr) const;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct View;
    void refresh();
    void refreshLocalizedIdentity();
    void refreshLocalizedDiagnostics();
    void refreshBackgroundEditor();
    bool applyBackground(QString *error);
    bool importImage(const QString &path, QString *error);
    bool importFont(const QString &path, QString *error);
    QJsonObject editedMetadata() const;

    ThemeCore &core_;
    ThemePackage candidate_;
    QString candidatePath_;
    QHash<QString, ThemePackage> importedImages_;
    bool previewing_ = false;
    std::unique_ptr<View> view_;
};

}
