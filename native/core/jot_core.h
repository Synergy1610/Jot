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

#ifdef __cplusplus
}
#endif
