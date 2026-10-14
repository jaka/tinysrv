#include "utils.h"

#include <ctype.h>  /* isprint(), isdigit(), tolower(), isalnum() */
#include <string.h> /* strlen() */

char *tu_strtok(char **str, unsigned int *match_length, const char *delimiters)
{
  char *s, *start;
  const char *d;

  if (!str || !*str || !match_length) return NULL;
  if (!strlen(delimiters)) return NULL;

  start = *str;
  *match_length = 0;

  for (s = *str; *s; s++) {
    for (d = delimiters; *d; d++) {
      if (*s == *d) {
        *str = s + 1;
        return start;
      }
    }
    (*match_length)++;
  }

  /* reached end of string without delimiter */
  *match_length = 0;
  return start;
}

char *tu_strbtok(char **str, unsigned int *match_length, const char *delimiter)
{
  char *start_ptr;
  unsigned int cur_delimiter, delimiter_length;

  if (!str || !*str || !match_length) return NULL;
  *match_length = 0;

  delimiter_length = strlen(delimiter);
  if (!delimiter_length) return NULL;

  start_ptr = *str;

  cur_delimiter = 0;
  while (**str) {
    (*match_length)++;
    if (**str == *(delimiter + cur_delimiter)) {
      cur_delimiter++;
      if (cur_delimiter == delimiter_length) {
        (*match_length) -= delimiter_length;
        (*str)++;
        break;
      }
    }
    else {
      if (**str == *delimiter) cur_delimiter = 1;
      else cur_delimiter = 0;
    }
    (*str)++;
  }

  if (cur_delimiter != delimiter_length) *match_length = 0;

  return start_ptr;
}

char *strstr_last(const char *const str1, const char *const str2)
{
  /*
  Search string [str2] from right to left in string [str1].
  */
  char *strp;
  int len1, len2;

  len2 = strlen(str2);
  if (!len2) return (char *)str1;

  len1 = strlen(str1);
  if (len1 < len2) return NULL;

  for (strp = (char *)(str1 + len1 - len2); strp != str1; strp--) {
    if (*strp == *str2 && !strncmp(strp, str2, len2)) return strp;
  }
  if (!strncmp(str1, str2, len2)) return (char *)str1;

  return NULL;
}

static char from_hex(const char ch)
{
  if (isdigit((unsigned char)ch)) return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return 0;
}

void decode_url(char *to, const char *from)
{
  while (*from) {
    /* TODO: [1] and [2] may not exist */
    if (*from == '%' && from[1] && from[2] && isxdigit(from[1]) && isxdigit(from[2])) {
      *to++ = from_hex(*(from + 1)) << 4 | from_hex(*(from + 2));
      from += 3;
      continue;
    }
    *to++ = *from++;
  }
  *to = 0;
}

int change_dots_to_underscore(char *const str)
{
  char *strp;
  int dot_count;

  dot_count = 0;
  for (strp = str; *strp; strp++) {
    if (*strp == '.') {
      *strp = '_';
      dot_count++;
    }
    else if (!isalnum((unsigned char)*strp) && *strp != '-') return -1;
  }
  return dot_count;
}

int is_safe_filename(const char *const str)
{
  char *strp;

  for (strp = (char *)str; *strp; strp++) {
    if (!isprint((unsigned char)*strp) || *strp == '%') return 0;
    else if (*strp == '.') {
      if (*(strp + 1) == '.') return 0;
    }
    else if (*strp == '/') {
      if (*(strp + 1) == '/') return 0;
    }
  }
  return 1;
}

int ts_concatenate_path_filename(char *buf, const int buflen, const char *path, const char *filename)
{
  char *p;
  int len, remaining;

  if (!buf || buflen <= 0 || !filename) return -1;

  p = buf;
  remaining = buflen;

  *p = 0;

  /* copy path if present */
  if (path && *path) {
    len = (int)strlen(path);
    if (len >= remaining) return -1;

    memcpy(p, path, len);
    p += len;
    remaining -= len;

    /* ensure trailing '/' */
    if (*(p - 1) != '/') {
      if (remaining < 2) return -1; /* '/' + '\0' */
      *p++ = '/';
      remaining--;
    }
    *p = 0;
  }

  /* copy filename */
  len = (int)strlen(filename);
  if (len >= remaining) return -1;
  memcpy(p, filename, len);
  p += len;
  remaining -= len;
  *p = 0;

  return 0;
}
