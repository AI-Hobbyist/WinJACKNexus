#pragma once

#if defined(_WIN32)
#    if defined(WJN_PATCHBAY_MOD_BUILD)
#        define MOD_PATCHBAY_API __declspec(dllexport)
#    else
#        define MOD_PATCHBAY_API __declspec(dllimport)
#    endif
#else
#    define MOD_PATCHBAY_API
#endif