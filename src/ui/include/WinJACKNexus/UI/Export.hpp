#pragma once

#if defined(_WIN32)
#    if defined(WJN_UI_BUILD)
#        define WINJACK_UI_API __declspec(dllexport)
#    else
#        define WINJACK_UI_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_UI_API
#endif