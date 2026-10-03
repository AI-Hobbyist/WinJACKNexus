/* Shared example-side optional ABI, independent of the manager. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/I18nApi.h>
#include <QPointer>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <algorithm>
#include <functional>
#include <vector>

namespace ExampleI18n {
inline const WjnI18nHostV1* host = nullptr;
inline uint32_t events[3] {};
struct Binding { QPointer<QObject> owner; std::function<void()> apply; };
inline std::vector<Binding> bindings;
inline QString text(const char* domain, const char* key, const char* english) {
    if (!host) return QString::fromUtf8(english);
    uint32_t size=0;
    if (host->text(host->context,domain,key,english,nullptr,-1,nullptr,0,&size)!=WJN_I18N_OK)
        return QString::fromUtf8(english);
    QByteArray buffer(static_cast<qsizetype>(size),'\0');
    if (host->text(host->context,domain,key,english,nullptr,-1,buffer.data(),size,&size)!=WJN_I18N_OK)
        return QString::fromUtf8(english);
    return QString::fromUtf8(buffer.constData());
}
inline void bind(QObject* owner, std::function<void()> apply) {
    bindings.push_back({owner,apply}); apply();
}
inline int32_t attach(const WjnI18nHostV1* value) {
    if (!value || value->structSize<sizeof(WjnI18nHostV1) || value->apiVersion!=WJN_I18N_API_VERSION || !value->text)
        return WJN_I18N_INVALID_ARGUMENT;
    if (host && host!=value) return WJN_I18N_INVALID_ARGUMENT;
    host=value; ++events[0]; return WJN_I18N_OK;
}
inline void changed() {
    ++events[1];
    std::erase_if(bindings,[](const Binding& item) { return item.owner.isNull(); });
    for (const auto& binding : bindings) if (binding.owner) binding.apply();
}
inline void detach() { host=nullptr; bindings.clear(); ++events[2]; }
}

// Extra example-only probes used by offline DLL contract tests.
#ifdef WJN_NATIVE_FX_BUILD
#define WJN_EXAMPLE_EXPORT WJN_FX_API
#else
#define WJN_EXAMPLE_EXPORT WINJACK_NODE_API
#endif
extern "C" WJN_EXAMPLE_EXPORT uint32_t wjn_example_i18n_events(uint32_t event) {
    return event<3 ? ExampleI18n::events[event] : 0;
}
extern "C" WJN_EXAMPLE_EXPORT int32_t wjn_example_i18n_query(const char* domain, const char* key,
    const char* fallback, const char* args, int64_t count, char* buffer, uint32_t capacity, uint32_t* required) {
    if (!ExampleI18n::host) return WJN_I18N_INVALID_ARGUMENT;
    return ExampleI18n::host->text(ExampleI18n::host->context,domain,key,fallback,args,count,buffer,capacity,required);
}
int32_t wjn_i18n_attach_host(const WjnI18nHostV1* value) { return ExampleI18n::attach(value); }
void wjn_i18n_language_changed(const char*, uint64_t) { ExampleI18n::changed(); }
void wjn_i18n_detach_host() { ExampleI18n::detach(); }
