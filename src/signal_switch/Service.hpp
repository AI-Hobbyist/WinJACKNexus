/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include "State.hpp"
#include <QPointer>
#include <WinJACKNexus/Common/SignalSwitchProcessor.hpp>
#include <functional>

class QWidget;
class QMenu;

#ifdef WJN_SIGNAL_SWITCH_BUILD
#define WJN_SWITCH_API __declspec(dllexport)
#else
#define WJN_SWITCH_API __declspec(dllimport)
#endif

namespace WinJACKNexus::SignalSwitch
{
class Sound;
struct Endpoint
{
    QString id;
    QString name;
    QString context;
    QString version;
    Common::SignalSwitch::Gate *gate{};
    bool desired{};
    std::function<bool(const QString &)> rename;
    QString graphClient;
    QString domain;
    bool userNamed{};
    Sound *sound{};
    QString shortcut;
    QString shortcutError;
    std::atomic<float> *peak{};
    bool peakVertical{};
    std::atomic<float> *gainLinear{};
    double gainDb{};
    double gainMinimum{-72};
    double gainMaximum{12};
    std::function<void()> editValue;
};

// One service in control_deck.dll, shared by every node DLL and the host.
class WJN_SWITCH_API Service
{
  public:
    static Service &instance();
    State state;
    QMap<void *, Endpoint> endpoints;
    std::function<void()> changed;
    std::function<void()> edited;
    std::function<void()> synchronize;

    void attach(void *node, Endpoint endpoint);
    void detach(void *node);
    void bind(void *node, const QString &id, const QString &name, const QString &context);
    bool setEnabled(const QStringList &ids, bool enabled, QString *error = nullptr);
    void activate(const QString &id);
    bool setShortcut(const QString &id, const QString &sequence, QString *error = nullptr);
    void updateShortcuts();
    void pollSounds();
    bool setGain(const QString &id, double db);
    bool setGainRange(const QString &id, double minimum, double maximum);
    bool restoreDesired(void *node, bool enabled);
    void reconcile();
    void notify(bool edit = false);
    bool restore(const QString &base64, QString *error = nullptr);
    void reset();
    Endpoint *find(const QString &id);
    QString label(const Endpoint &endpoint) const;
    void beginRestore();
    bool restoring() const
    {
        return restoring_;
    }
    bool installUI(QString *error = nullptr);
    void shutdownUI();
    QWidget *showEditor();
    QMenu *quickMenu();
    void refreshUIState();

  private:
    bool restoring_{};
    QPointer<QWidget> editor_;
};
} // namespace WinJACKNexus::SignalSwitch
