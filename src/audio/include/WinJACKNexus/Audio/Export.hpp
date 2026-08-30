#pragma once

#if defined(_WIN32)
#    if defined(WJN_AUDIO_BUILD)
#        define WINJACK_AUDIO_API __declspec(dllexport)
#    else
#        define WINJACK_AUDIO_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_AUDIO_API
#endif