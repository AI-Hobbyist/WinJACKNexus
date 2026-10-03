/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/I18nManager.hpp>

namespace WinJACKNexus::UI {
// Domain-aware display helpers. Identity and user-editable values stay outside
// these bindings; the QObject context disconnects automatically on destruction.
inline QString localizedText(const char* domain,const char* key,const char* english,
                             const QVariantMap& arguments = {}) {
    if (const auto* manager=I18nManager::installed())
        return manager->text(QString::fromUtf8(domain),QString::fromUtf8(key),QString::fromUtf8(english),arguments);
    QString result;
    return I18nManager::format(QString::fromUtf8(english),arguments,-1,&result)
        ?result:QString::fromUtf8(english);
}
inline void bindLocalizedText(QObject* object,const char* property,const char* domain,
                              const char* key,const char* english,const QVariantMap& arguments = {}) {
    if (!object) return;
    const QByteArray p(property),d(domain),k(key),e(english);
    object->setProperty((QByteArray("wjn.i18n.")+p).constData(),k);
    object->setProperty((QByteArray("wjn.i18nDomain.")+p).constData(),d);
    const auto apply=[object,p,d,k,e,arguments] {
        object->setProperty(p.constData(),localizedText(d.constData(),k.constData(),e.constData(),arguments));
    };
    if (auto* manager=I18nManager::installed()) manager->bind(object,apply);
    else apply();
}
}
