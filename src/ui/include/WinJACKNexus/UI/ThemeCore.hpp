/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/UI/ThemePackage.hpp>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSharedPointer>
#include <QStringList>

namespace WinJACKNexus::UI {

struct WINJACK_UI_API ThemeDiagnostic final {
    QString code;
    QString token;
    QString message;
    int line = 0;
};

struct WINJACK_UI_API ThemeToken final {
    QString name;
    QString ownerScope;
    QString type;
    QString defaultValue;
    bool hasDefault = false;
    QStringList inherits;
    QString description;
};

class WINJACK_UI_API ThemeRegistry final {
public:
    static bool fromJson(const QByteArray& json, ThemeRegistry* result, QString* error = nullptr);
    static bool builtIn(ThemeRegistry* result, QString* error = nullptr);
    static bool valueMatchesType(const QString& type, const QString& value);

    bool contains(const QString& token) const;
    ThemeToken token(const QString& name) const;
    QStringList names() const;
    bool withTokens(const QList<ThemeToken>& additions, ThemeRegistry* result,
                    QString* error = nullptr) const;

private:
    QHash<QString, ThemeToken> tokens_;
};

class WINJACK_UI_API ThemeSnapshot final {
public:
    QString themeId() const;
    bool hasValue(const QString& token) const;
    QString value(const QString& token) const;
    ThemePackage::ResourceRef resource(const QString& token) const;
    QList<ThemeDiagnostic> diagnostics() const;

private:
    friend class ThemeCore;
    struct Entry { QString value; ThemePackage::ResourceRef resource; };
    QString themeId_;
    QHash<QString, Entry> entries_;
    QList<ThemeDiagnostic> diagnostics_;
};

// S04 data core. Painting and legacy ThemeManager adaptation follow in S05/S07.
class WINJACK_UI_API ThemeCore final : public QObject {
    Q_OBJECT
public:
    explicit ThemeCore(QObject* parent = nullptr);
    explicit ThemeCore(ThemeRegistry registry, QObject* parent = nullptr);

    bool isValid() const noexcept;
    QString initializationError() const;
    ThemePackage defaultTheme() const;
    QSharedPointer<const ThemeSnapshot> snapshot() const;
    QSharedPointer<const ThemeSnapshot> previewBaseSnapshot() const;
    bool setFullTheme(const ThemePackage& package, QString* error = nullptr);
    bool applyFragment(const ThemePackage& package, QString* error = nullptr);
    bool setOverride(const QString& token, const QString& value,
                     const ThemePackage& resourceOwner = {}, QString* error = nullptr);
    bool removeOverride(const QString& token);
    bool beginPreview(const ThemePackage& package, QString* error = nullptr);
    bool cancelPreview();
    bool commitPreview();
    void restoreDefault();
    bool refreshRegisteredNamespaces(QString* error = nullptr);
    ThemePackage activeFullTheme() const;
    QString declaredValue(const QString& token) const;
    bool exportFullTheme(const QJsonObject& metadata, ThemePackage* result,
                         QString* error = nullptr) const;
    bool exportFragment(const QString& namespacePrefix, const QJsonObject& metadata,
                        ThemePackage* result, QString* error = nullptr) const;
    bool hasUserOverrides() const;
    bool exportUserOverrides(ThemePackage* result, QString* error = nullptr) const;
    bool restoreUserOverrides(const ThemePackage& package, QString* error = nullptr);

Q_SIGNALS:
    void themeChanged();
    void previewCompleted();

private:
    struct State;
    bool activate(State state, QString* error);
    bool exportPackage(const QString& namespacePrefix, const QJsonObject& metadata,
                       ThemePackage* result, QString* error) const;
    ThemeRegistry registry_;
    ThemeRegistry baseRegistry_;
    ThemePackage defaultTheme_;
    QString initializationError_;
    QSharedPointer<const ThemeSnapshot> snapshot_;
    QSharedPointer<State> state_;
    QSharedPointer<State> previewPrevious_;
    QSharedPointer<const ThemeSnapshot> previewPreviousSnapshot_;
};

}
