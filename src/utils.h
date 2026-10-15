#ifndef _UTILS_H
#define _UTILS_H

char *tu_strtok(char **, unsigned int *, const char *);
char *tu_strbtok(char **, unsigned int *, const char *);

char *strstr_last(const char *const hay, const char *const needle);
void decode_url(char *to, const char *from);
int change_dots_to_underscore(char *const);
int is_safe_filename(const char *const str);
int ts_concatenate_path_filename(char *, int, const char *, const char *);

#endif
