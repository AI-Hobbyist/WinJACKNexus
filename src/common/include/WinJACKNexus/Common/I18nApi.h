/* Optional i18n v1 ABI. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        WJN_I18N_API_VERSION = 1u
    };
    enum WjnI18nStatus
    {
        WJN_I18N_OK = 0,
        WJN_I18N_INVALID_ARGUMENT = 1,
        WJN_I18N_BUFFER_TOO_SMALL = 2,
        WJN_I18N_FORBIDDEN_DOMAIN = 3,
        WJN_I18N_WRONG_THREAD = 4,
        WJN_I18N_INTERNAL_ERROR = 5
    };
    /* All strings UTF-8, borrowed for the duration of the call. requiredBytes
   includes NUL. NULL buffer + zero capacity queries size. Short buffers are
   not partially filled. argumentsJson is NULL or a JSON object of strings,
   numbers and bools (max 64 KiB). count=-1 disables plural selection. */
    typedef int32_t (*WjnI18nTextV1)(void *context, const char *domain, const char *key,
                                     const char *englishFallback, const char *argumentsJson,
                                     int64_t count, char *buffer, uint32_t capacity,
                                     uint32_t *requiredBytes);
    typedef struct WjnI18nHostV1
    {
        uint32_t structSize;
        uint32_t apiVersion;
        void *context;
        WjnI18nTextV1 text;
    } WjnI18nHostV1;
    typedef uint32_t (*WjnI18nVersionFn)(void);
    typedef const char *(*WjnI18nDescriptorFn)(uint32_t index);
    /* Return WJN_I18N_OK or a WjnI18nStatus. The host table is valid through
   detach. Attach/change/detach run on the host GUI thread, once per DLL. */
    typedef int32_t (*WjnI18nAttachFn)(const WjnI18nHostV1 *host);
    typedef void (*WjnI18nChangedFn)(const char *effectiveLanguage, uint64_t revision);
    typedef void (*WjnI18nDetachFn)(void);
#ifdef __cplusplus
}
#endif
