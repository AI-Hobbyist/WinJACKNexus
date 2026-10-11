// Optional DLL language lifecycle, only accessed on the host GUI thread.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/I18nApi.h>
namespace WinJACKNexus::MidiTools::I18n {
inline const WjnI18nHostV1* host{};
inline int32_t attach(const WjnI18nHostV1* h) {
    if(!h || h->structSize<sizeof(*h) || h->apiVersion!=1 || !h->text || (host && host!=h))return WJN_I18N_INVALID_ARGUMENT;
    host=h;return WJN_I18N_OK;
}
inline void detach(){host=nullptr;}
}
