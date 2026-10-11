/* Host startup, Qt translations and shared selection model. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <QTranslator>
class QComboBox;
class QMenu;
class QWidget;

namespace WinJACKNexus::UI
{
class WINJACK_UI_API HostI18n final : public QObject
{
    Q_OBJECT
  public:
    explicit HostI18n(I18nOptions options = {}, QObject *parent = nullptr);
    ~HostI18n() override;
    // Before any application-owned window, menu or startup message.
    bool initialize(const QString &absoluteExecutableDirectory, QString *error = nullptr);
    static HostI18n *installed();
    I18nManager &manager();
    QMenu *createLanguageMenu(QWidget *parent = nullptr);
    void bindLanguageChoice(QComboBox *choice);
    static QString languageDisplayName(const QString &selection);
  Q_SIGNALS:
    void languageSelectionFailed(const QString &detail);

  private:
    void updateQtTranslation();
    I18nManager manager_;
    I18nDomainLease hostLease_;
    QTranslator qtTranslator_;
    QString root_;
    bool qtInstalled_ = false;
};
// English argument is mandatory even when no manager is available.
WINJACK_UI_API QString hostText(const char *key, const char *english,
                                const QVariantMap &arguments = {});
WINJACK_UI_API void bindHostText(QObject *object, const char *property, const char *key,
                                 const char *english);
}
