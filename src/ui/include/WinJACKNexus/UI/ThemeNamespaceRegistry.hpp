/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLibrary>
#include <QMutex>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace WinJACKNexus::UI {

enum class ThemeModuleKind { nativeFx, node, internal };
using ThemeSymbolResolver = std::function<QFunctionPointer(const char*)>;

struct WINJACK_UI_API ThemeNamespaceDescriptor final {
    QString moduleId;
    QString namespacePrefix;
    QString displayName;
    QList<ThemeToken> tokens;
    QJsonObject metadata;
    QByteArray canonicalJson;
};

class WINJACK_UI_API ThemeNamespaceRegistry final {
public:
    class WINJACK_UI_API Lease final {
    public:
        Lease() = default;
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        void reset();
        bool isEmpty() const noexcept { return keys_.isEmpty(); }

    private:
        friend class ThemeNamespaceRegistry;
        ThemeNamespaceRegistry* registry_ = nullptr;
        QStringList keys_;
    };

    ThemeNamespaceRegistry() = default;
    ~ThemeNamespaceRegistry() = default;
    ThemeNamespaceRegistry(const ThemeNamespaceRegistry&) = delete;
    ThemeNamespaceRegistry& operator=(const ThemeNamespaceRegistry&) = delete;
    static ThemeNamespaceRegistry& instance();

    // Call while the module is loaded; all strings are copied before return.
    bool registerNativeFxModule(const ThemeSymbolResolver& resolve, const QString& source,
                                Lease* result, QString* error = nullptr);
    bool registerNodeModule(const ThemeSymbolResolver& resolve, const QString& source,
                            Lease* result, QString* error = nullptr);
    bool registerInternal(const QByteArray& utf8Json, const QString& source,
                          Lease* result, QString* error = nullptr);

    QSharedPointer<const ThemeNamespaceDescriptor> descriptor(const QString& moduleId) const;
    QString scopeFor(ThemeModuleKind kind, const QString& source, quint32 index) const;
    int referenceCount(const QString& moduleId) const;
    QStringList moduleIds() const;

private:
    struct Entry {
        QSharedPointer<const ThemeNamespaceDescriptor> descriptor;
        QString source;
        int references = 0;
    };
    struct Binding { QString moduleId; int references = 0; };
    struct Candidate {
        QString bindingKey;
        QSharedPointer<const ThemeNamespaceDescriptor> descriptor;
    };
    bool registerCandidates(const std::vector<Candidate>& candidates, const QString& source,
                            Lease* result, QString* error);
    void release(const QStringList& keys);
    static QString bindingKey(ThemeModuleKind kind, const QString& source, quint32 index);

    mutable QMutex mutex_;
    QHash<QString, Entry> entries_;
    QHash<QString, Binding> bindings_;
};

}
