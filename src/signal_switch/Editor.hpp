/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include "Service.hpp"
#include <QMenu>
#include <QTimer>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>

namespace WinJACKNexus::SignalSwitch
{
class Editor : public QWidget
{
  public:
    explicit Editor(Service &service);
    ~Editor() override;
    void refresh();
    void restoreGeometryFromState();
    QMenu *menu() const
    {
        return menu_;
    }

  protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    Service &service_;
    QWidget *canvas_{};
    UI::Controls::ScrollField *scroll_{};
    UI::Controls::TextLabel *empty_{};
    QMap<QString, UI::Controls::ActionButton *> buttons_;
    QMenu *menu_{};
    QTimer maintenance_;
    bool applying_{};
    void rememberGeometry();
    void rename(const QString &id);
};
} // namespace WinJACKNexus::SignalSwitch
