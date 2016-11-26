#ifndef _UTILS_H
#define _UTILS_H

unsigned int find_delimiter(const char *startptr, char **endptr, const char *delimiters);
char *strnstr(const char *str, const char *needle, int len);
char *strstr_last(const char* const str1, const char* const str2);
void decode_url(char *to, const char *from);
int change_dots_to_underscore(char *const);
int is_safe_filename(const char *const str);
int ts_concatenate_path_filename(char *, int, const char *, const char *);

#endif
