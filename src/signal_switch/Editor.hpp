/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include "Service.hpp"
#include <QMenu>
#include <QRubberBand>
#include <QSet>
#include <QTimer>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <Windows.h>

namespace WinJACKNexus::SignalSwitch
{
class Editor : public QWidget
{
  public:
    explicit Editor(Service &service);
    ~Editor() override;
    void refresh();
    void restoreGeometryFromState();
    void setLocked(bool locked, bool capturePosition = true);
    QMenu *menu() const
    {
        return menu_;
    }

  protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    Service &service_;
    QWidget *canvas_{};
    UI::Controls::ScrollField *scroll_{};
    UI::Controls::TextLabel *empty_{};
    QMap<QString, UI::Controls::ActionButton *> buttons_;
    QMenu *menu_{};
    QTimer maintenance_;
    bool applying_{};
    QSet<QString> selected_;
    QSet<QString> selectionBase_;
    QMap<QString, UI::Controls::GroupPanel *> groupPanels_;
    QMap<QString, QWidget *> overlays_;
    QRubberBand *rubber_{};
    QPoint pressPosition_;
    QMap<QString, QPoint> dragStarts_;
    bool dragging_{};
    bool selecting_{};
    void *mouseHook_{};
    QString hookPressed_;
    QAction *lockAction_{};
    QAction *unlockAction_{};
    QAction *throughAction_{};
    UI::Controls::TextLabel *status_{};
    void rememberGeometry();
    void rename(const QString &id);
    void showCardMenu(const QString &id, const QPoint &global);
    void showCanvasMenu(const QPoint &global);
    void showGroupMenu(const QString &id, const QPoint &global);
    void refreshGroups();
    void refreshPresentation();
    void styleButton(UI::Controls::ActionButton *button, const QString &id);
    void editSize();
    void editColor();
    void editGrid();
    void editOpacity();
    void createGroup();
    void createRelation();
    bool editText(const char *key, const char *english, QString *value);
    void reportConflict();
    bool refreshControl(UI::Controls::ActionButton *card, const Endpoint &endpoint);
    void loadSound(const QString &id);
    void editShortcut(const QString &id);
    void editGainRange(const QString &id);
    void editGainValue(const QString &id);
    void updateMouseHook();
    void releaseMouseHook();
    static LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data);
};
} // namespace WinJACKNexus::SignalSwitch
