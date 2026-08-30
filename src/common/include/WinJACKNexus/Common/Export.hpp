#pragma once

#if defined(_WIN32)
#    if defined(WJN_COMMON_BUILD)
#        define WINJACK_COMMON_API __declspec(dllexport)
#    else
#        define WINJACK_COMMON_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_COMMON_API
#endif