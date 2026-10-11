/* Compile the service table as actual C, without Qt or STL. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/I18nApi.h>
#include <stddef.h>
_Static_assert(offsetof(WjnI18nHostV1, structSize) == 0, "size at offset zero");
_Static_assert(offsetof(WjnI18nHostV1, apiVersion) == 4, "version after size");
_Static_assert(sizeof(((WjnI18nHostV1 *)0)->apiVersion) == 4, "32-bit version");
int wjn_test_i18n_c_abi(void)
{
    WjnI18nHostV1 service = {sizeof(WjnI18nHostV1), WJN_I18N_API_VERSION, 0, 0};
    return service.structSize == sizeof(WjnI18nHostV1) && service.apiVersion == 1 &&
           WJN_I18N_INTERNAL_ERROR == 5;
}
