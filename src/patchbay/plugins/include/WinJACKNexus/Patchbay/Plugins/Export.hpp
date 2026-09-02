#pragma once

#if defined(_WIN32)
#    if defined(WJN_PATCHBAY_PLUGINS_BUILD)
#        define WINJACK_PATCHBAY_PLUGINS_API __declspec(dllexport)
#    else
#        define WINJACK_PATCHBAY_PLUGINS_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_PATCHBAY_PLUGINS_API
#endif