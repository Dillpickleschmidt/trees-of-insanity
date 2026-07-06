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

#ifdef __cplusplus
}
#endif
