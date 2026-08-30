#pragma once

#if defined(_WIN32)
#    if defined(WJN_PLUGIN_BUILD)
#        define WINJACK_PLUGIN_API __declspec(dllexport)
#    else
#        define WINJACK_PLUGIN_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_PLUGIN_API
#endif