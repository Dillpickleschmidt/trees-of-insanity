#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ToiNativeCore ToiNativeCore;

ToiNativeCore* toi_create(const char* options_json);
void toi_destroy(ToiNativeCore* core);

char* toi_handle_command(ToiNativeCore* core, const char* request_json);
char* toi_last_error_json(void);
void toi_free_string(char* value);

/* Attach the Vulkan viewport to the shell's native X11 window (the Electrobun
 * WGPU child window XID). Starts a present loop. Returns a JSON result string
 * ({ok:true, ...} or {ok:false, error}). Only functional when the core is built
 * with viewport support; otherwise returns an error. */
char* toi_attach_x11_viewport(ToiNativeCore* core, unsigned long x_window, int width, int height);
char* toi_detach_viewport(ToiNativeCore* core);

#ifdef __cplusplus
}
#endif
