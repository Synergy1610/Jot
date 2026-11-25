#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct JotCorePad JotCorePad;

bool jot_core_create(const char *path, char **error);
JotCorePad *jot_core_open(const char *path, char **error);
char *jot_core_load(JotCorePad *pad, char **error);
bool jot_core_save(JotCorePad *pad, const char *expected, const char *content, char **error);
bool jot_core_migrate(JotCorePad *pad, char **error);
bool jot_core_backup(JotCorePad *pad, const char *path, char **error);
bool jot_core_clear(JotCorePad *pad, const char *expected, const char *current, char **error);
char *jot_core_last_cleared(JotCorePad *pad, char **error);
bool jot_core_undo_clear(JotCorePad *pad, const char *backup, char **error);
void jot_core_close(JotCorePad *pad);
void jot_core_string_free(char *value);

bool jot_cloud_configure(const char *url, const char *anon_key, char **error);
bool jot_cloud_restore(char **error);
bool jot_cloud_is_signed_in(void);
char *jot_cloud_account(void);
bool jot_cloud_sign_in(const char *email, const char *password, char **error);
bool jot_cloud_sign_up(const char *email, const char *password, bool *needs_confirmation, char **error);
bool jot_cloud_sign_out(char **error);
bool jot_cloud_download(char **content, char **updated_at, long long *version, char **error);
bool jot_cloud_upload(const char *content, long long version, long long *new_version, char **error);

#ifdef __cplusplus
}
#endif
