// Optional DLL language lifecycle, only accessed on the host GUI thread.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/I18nApi.h>
#include <QPointer>
#include <QObject>
#include <QString>
#include <QByteArray>
#include <functional>
#include <vector>
#include <algorithm>
namespace WinJACKNexus::MidiTools::I18n {
inline const WjnI18nHostV1* host{};
struct Binding {QPointer<QObject> owner;std::function<void()> apply;};
inline std::vector<Binding> bindings;
inline QString text(const char* domain,const char* key,const char* fallback){
    uint32_t size{};
    if(!host || host->text(host->context,domain,key,fallback,nullptr,-1,nullptr,0,&size)!=WJN_I18N_OK || !size || size>32769)return QString::fromUtf8(fallback);
    QByteArray buffer(size,'\0');
    return host->text(host->context,domain,key,fallback,nullptr,-1,buffer.data(),size,&size)==WJN_I18N_OK?QString::fromUtf8(buffer.constData()):QString::fromUtf8(fallback);
}
inline void changed(){std::erase_if(bindings,[](const auto& b){return !b.owner;});const auto copy=bindings;for(const auto& b:copy)if(b.owner)b.apply();}
inline void bind(QObject* owner,std::function<void()> apply){bindings.push_back({owner,apply});apply();}
inline int32_t attach(const WjnI18nHostV1* h) {
    if(!h || h->structSize<sizeof(*h) || h->apiVersion!=1 || !h->text || (host && host!=h))return WJN_I18N_INVALID_ARGUMENT;
    host=h;return WJN_I18N_OK;
}
inline void detach(){host=nullptr;changed();}
}
