/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Service.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QKeySequence>
#include <QObject>
#include <Windows.h>
#include <algorithm>

namespace WinJACKNexus::SignalSwitch
{
namespace
{
struct Key
{
    UINT modifiers{}, key{};
};

bool parseKey(const QString &text, Key *result)
{
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown)
        return false;
    const auto combination = sequence[0];
    const auto key = combination.key();
    UINT native{};
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        native = UINT(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        native = VK_F1 + UINT(key - Qt::Key_F1);
    else
    {
        switch (key)
        {
        case Qt::Key_Space:
            native = VK_SPACE;
            break;
        case Qt::Key_Return:
            native = VK_RETURN;
            break;
        case Qt::Key_Tab:
            native = VK_TAB;
            break;
        case Qt::Key_Escape:
            native = VK_ESCAPE;
            break;
        case Qt::Key_Insert:
            native = VK_INSERT;
            break;
        case Qt::Key_Delete:
            native = VK_DELETE;
            break;
        case Qt::Key_Home:
            native = VK_HOME;
            break;
        case Qt::Key_End:
            native = VK_END;
            break;
        case Qt::Key_PageUp:
            native = VK_PRIOR;
            break;
        case Qt::Key_PageDown:
            native = VK_NEXT;
            break;
        case Qt::Key_Left:
            native = VK_LEFT;
            break;
        case Qt::Key_Right:
            native = VK_RIGHT;
            break;
        case Qt::Key_Up:
            native = VK_UP;
            break;
        case Qt::Key_Down:
            native = VK_DOWN;
            break;
        default:
            return false;
        }
    }
    const auto modifiers = combination.keyboardModifiers();
    if (modifiers & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier))
        return false;
    result->modifiers = MOD_NOREPEAT;
    if (modifiers & Qt::ControlModifier)
        result->modifiers |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier)
        result->modifiers |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier)
        result->modifiers |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier)
        result->modifiers |= MOD_WIN;
    result->key = native;
    return true;
}

class Hotkeys final : public QObject, public QAbstractNativeEventFilter
{
  public:
    explicit Hotkeys(QObject *parent) : QObject(parent)
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
    ~Hotkeys() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        for (const auto &registration : registrations)
            UnregisterHotKey(nullptr, registration.nativeId);
    }
    struct Registration
    {
        int nativeId{};
        QString sequence;
    };
    QMap<QString, Registration> registrations;
    int nextId{0x4000};

    bool replace(const QString &id, const QString &sequence, QString *error)
    {
        const auto old = registrations.value(id);
        if (old.sequence == sequence)
            return true;
        if (sequence.isEmpty())
        {
            if (old.nativeId)
                UnregisterHotKey(nullptr, old.nativeId);
            registrations.remove(id);
            return true;
        }
        Key key;
        if (!parseKey(sequence, &key))
        {
            if (error)
                *error = "Invalid shortcut. Use one combination, for example Ctrl+Alt+1.";
            return false;
        }
        if (nextId > 0xbfff)
            nextId = 0x4000;
        while (nextId <= 0xbfff && std::any_of(registrations.cbegin(), registrations.cend(),
                                               [this](const auto &item) { return item.nativeId == nextId; }))
            ++nextId;
        if (nextId > 0xbfff || !RegisterHotKey(nullptr, nextId, key.modifiers, key.key))
        {
            if (error)
                *error = "Shortcut is already in use or reserved by Windows.";
            return false;
        }
        if (old.nativeId)
            UnregisterHotKey(nullptr, old.nativeId);
        registrations[id] = {nextId++, sequence};
        return true;
    }

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        const auto *event = static_cast<const MSG *>(message);
        if (event->message != WM_HOTKEY)
            return false;
        for (auto it = registrations.cbegin(); it != registrations.cend(); ++it)
        {
            if (it->nativeId != int(event->wParam))
                continue;
            Service::instance().activate(it.key());
            // Qt's windows_dispatcher_MSG path may omit the result pointer.
            if (result)
                *result = 0;
            return true;
        }
        return false;
    }
};

Hotkeys &hotkeys()
{
    static QPointer<Hotkeys> registry;
    if (!registry)
        registry = new Hotkeys(QCoreApplication::instance());
    return *registry;
}
} // namespace

bool Service::setShortcut(const QString &id, const QString &sequence, QString *error)
{
    auto *endpoint = find(id);
    if (!endpoint || !endpoint->sound)
        return false;
    const auto input = sequence.trimmed();
    Key parsed;
    if (!input.isEmpty() && !parseKey(input, &parsed))
    {
        if (error)
            *error = "Invalid shortcut combination";
        return false;
    }
    const auto normalized =
        QKeySequence::fromString(input, QKeySequence::PortableText).toString(QKeySequence::PortableText);
    if (!hotkeys().replace(id, normalized, error))
        return false;
    endpoint->shortcut = normalized;
    endpoint->shortcutError.clear();
    notify(true);
    return true;
}

void Service::updateShortcuts()
{
    auto &registry = hotkeys();
    QStringList live;
    if (!restoring())
        for (auto &endpoint : endpoints)
        {
            if (!endpoint.sound || endpoint.shortcut.isEmpty())
                continue;
            live.append(endpoint.id);
            if (!endpoint.shortcutError.isEmpty())
                continue;
            registry.replace(endpoint.id, endpoint.shortcut, &endpoint.shortcutError);
        }
    for (const auto &id : registry.registrations.keys())
        if (!live.contains(id))
            registry.replace(id, {}, nullptr);
}
} // namespace WinJACKNexus::SignalSwitch
