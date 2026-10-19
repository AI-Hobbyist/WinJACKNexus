/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QRect>
#include <QStringList>

namespace WinJACKNexus::SignalSwitch
{
struct Card
{
    QRect geometry{0, 0, 160, 48};
    QColor color;
};

struct Group
{
    QString name;
    QStringList members;
};

struct WindowSettings
{
    QRect geometry{100, 100, 640, 400};
    bool clickThrough{};
    int opacity{100};
    QString screen;
    QPoint scroll;
    QPoint overlayOrigin{100, 100};
};

// GUI-thread value model. Names and gate states remain owned by graph nodes.
class State
{
  public:
    QMap<QString, Card> cards;
    QMap<QString, Group> groups;
    QMap<QString, QStringList> exclusive;
    WindowSettings window;
    bool locked{};
    bool grid{};
    int gridX{16};
    int gridY{16};

    void add(const QString &id);
    void remove(const QString &id);
    bool setExclusive(const QString &id, const QStringList &members, QString *error = nullptr);
    void makeIndependent(const QStringList &members);
    bool setGroup(const QString &id, const QString &name, const QStringList &members, QString *error = nullptr);
    void ungroup(const QStringList &members);
    bool resolve(const QMap<QString, bool> &current, const QStringList &selection, bool enabled,
                 QMap<QString, bool> *result, QString *error = nullptr) const;
    QPoint snapped(QPoint point) const;
    bool validate(QString *error = nullptr) const;
    QJsonObject toJson() const;
    QString toBase64() const;
    static bool fromBase64(const QString &encoded, State *result, QString *error = nullptr);
};
} // namespace WinJACKNexus::SignalSwitch
