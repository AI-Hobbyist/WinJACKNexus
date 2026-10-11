/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <functional>

namespace VstI18n
{
// Retain the key and arguments so an existing status can change language.
class Text
{
  public:
    Text() = default;
    explicit Text(const QString &raw) : english_(raw.toUtf8()) {}
    Text(const char *key, const char *english) : key_(key), english_(english) {}
    Text &operator=(const QString &raw)
    {
        key_.clear();
        english_ = raw.toUtf8();
        values_.clear();
        return *this;
    }
    Text arg(const QString &value) const
    {
        auto copy = *this;
        copy.values_.push_back(value);
        return copy;
    }
    Text arg(int value) const
    {
        return arg(QString::number(value));
    }
    QString string() const;
    operator QString() const
    {
        return string();
    }
    bool isEmpty() const
    {
        return string().isEmpty();
    }

  private:
    QByteArray key_, english_;
    QStringList values_;
};
inline Text text(const char *key, const char *english)
{
    return {key, english};
}
void bind(QObject *owner, std::function<void()> apply);
void property(QObject *owner, const char *property, const Text &value);
}
