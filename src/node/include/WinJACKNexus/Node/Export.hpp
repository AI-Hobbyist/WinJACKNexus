#pragma once

#if defined(_WIN32)
#    if defined(WJN_NODE_BUILD) || defined(WJN_NODE_IMPLEMENTATION)
#        define WINJACK_NODE_API __declspec(dllexport)
#    else
#        define WINJACK_NODE_API __declspec(dllimport)
#    endif
#else
#    define WINJACK_NODE_API
#endif