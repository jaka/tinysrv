#ifndef _UTILS_H
#define _UTILS_H

char *tu_strtok(char **, unsigned int *, const char *);
char *tu_strbtok(char **, unsigned int *, const char *);

char *strstr_last(const char *const, const char *const);
void decode_url(char *, const char *);
int change_dots_to_underscore(char *const);
int is_safe_filename(const char *const);
int ts_concatenate_path_filename(char *, int, const char *, const char *);

#endif
