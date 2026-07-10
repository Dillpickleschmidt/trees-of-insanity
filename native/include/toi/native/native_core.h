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

/* Attach the Vulkan viewport to Electrobun's native WGPU child window. The
 * platform build interprets native_window as its native window handle. */
char* toi_attach_viewport(ToiNativeCore* core, void* native_window, int width, int height);
/* Rebuild the active Growth preview at the native viewport's physical pixel
 * resolution after its resize has settled. */
char* toi_resize_viewport(ToiNativeCore* core, int width, int height);
void toi_viewport_surface_changed(ToiNativeCore* core);
void toi_viewport_camera_input(ToiNativeCore* core, int kind, float dx, float dy, int viewport_height);
char* toi_viewport_status(ToiNativeCore* core);
void toi_detach_viewport(ToiNativeCore* core);

#ifdef __cplusplus
}
#endif
