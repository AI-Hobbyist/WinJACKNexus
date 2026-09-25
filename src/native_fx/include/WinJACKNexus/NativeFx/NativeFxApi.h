/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <stdint.h>

#if defined(_WIN32)
# if defined(WJN_NATIVE_FX_BUILD)
#  define WJN_FX_API __declspec(dllexport)
# else
#  define WJN_FX_API __declspec(dllimport)
# endif
#else
# define WJN_FX_API
#endif

#ifdef __cplusplus
class QWidget;
extern "C" {
#endif

enum { WJN_FX_MIXER = 1u, WJN_FX_NODE = 2u, WJN_FX_API_VERSION = 1u };
typedef struct WjnFxMeta {
    const char* id;
    const char* name;
    const char* iconGlyph;
    const char* iconColor;
    uint32_t placements;
} WjnFxMeta;

/* A mixer instance has no JACK client or ports. The host owns it and calls
   prepare/process in its own audio graph. Control changes must not overlap
   process() on another thread. */
typedef struct WjnFxInstance WjnFxInstance;
typedef struct WjnFxTheme {
    const char* background;
    const char* panel;
    const char* text;
    const char* accent;
    const char* border;
} WjnFxTheme;

WJN_FX_API uint32_t wjn_fx_api_version(void);
WJN_FX_API uint32_t wjn_fx_count(void);
WJN_FX_API const WjnFxMeta* wjn_fx_meta(uint32_t index);
WJN_FX_API WjnFxInstance* wjn_fx_create_mixer(uint32_t index);
/* Copies the module's per-channel parameters into a fresh instance. Audio
   history and the editor are reset; the host owns the returned instance. */
WJN_FX_API WjnFxInstance* wjn_fx_clone_mixer(const WjnFxInstance* source);
WJN_FX_API void wjn_fx_destroy_mixer(WjnFxInstance* instance);
WJN_FX_API void wjn_fx_prepare(WjnFxInstance* instance, double sampleRate);
WJN_FX_API void wjn_fx_process(WjnFxInstance* instance, uint32_t channel,
                               const float* input, float* output, uint32_t frames);
WJN_FX_API void wjn_fx_set_channel(WjnFxInstance* instance, int channel);
WJN_FX_API void wjn_fx_set_theme(WjnFxInstance* instance, const WjnFxTheme* theme);
/* Optional preset bridge used by the mixer host. Names and indices match the editor. */
WJN_FX_API uint32_t wjn_fx_preset_count(void);
WJN_FX_API const char* wjn_fx_preset_name(uint32_t index);
WJN_FX_API int wjn_fx_current_preset(const WjnFxInstance* instance);
WJN_FX_API void wjn_fx_select_preset(WjnFxInstance* instance, int index);
#ifdef __cplusplus
/* Editor belongs to the instance. In mixer placement it omits the channel
   strip; the chain host supplies one shared selector. */
WJN_FX_API QWidget* wjn_fx_create_mixer_editor(WjnFxInstance* instance, QWidget* parent);
#else
WJN_FX_API void* wjn_fx_create_mixer_editor(WjnFxInstance* instance, void* parent);
#endif

#ifdef __cplusplus
}
#endif
