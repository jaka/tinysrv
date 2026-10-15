#include "utils.h"

#include <ctype.h>  /* isprint(), isdigit(), tolower(), isalnum() */
#include <string.h> /* strlen() */

char *tu_strtok(char **str, unsigned int *match_length, const char *delimiters)
{
  char *scan, *start;
  const char *delim;

  if (!str || !*str || !match_length) return NULL;
  if (!*delimiters) return NULL;

  start = *str;
  *match_length = 0;

  for (scan = *str; *scan; scan++) {
    for (delim = delimiters; *delim; delim++) {
      if (*scan == *delim) {
        *str = scan + 1;
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

char *strstr_last(const char *const hay, const char *const needle)
{
  /*
  Search string [needle] from right to left in string [hay].
  */
  char *strp;
  int hay_len, needle_len;

  needle_len = strlen(needle);
  if (!needle_len) return (char *)hay;

  hay_len = strlen(hay);
  if (hay_len < needle_len) return NULL;

  for (strp = (char *)(hay + hay_len - needle_len); strp != hay; strp--) {
    if (*strp == *needle && !strncmp(strp, needle, needle_len)) return strp;
  }
  if (!strncmp(hay, needle, needle_len)) return (char *)hay;

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
