#pragma once

#if defined(_WIN32)
#    if defined(WJN_PATCHBAY_CORE_BUILD)
#        define WINJACK_PATCHBAY_API __declspec(dllexport)
#    else
#        define WINJACK_PATCHBAY_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_PATCHBAY_API
#endif