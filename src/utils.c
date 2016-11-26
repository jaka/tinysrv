#include "utils.h"

#include <ctype.h> /* isprint(), isdigit(), tolower(), isalnum() */
#include <string.h> /* strlen() */

unsigned int find_delimiter(const char *startptr, char **endptr, const char *delimiters) {

  char *ptr;
  unsigned int cur_delimiter, delimiters_length, match_length;

  delimiters_length = strlen(delimiters);

  if ( delimiters_length == 0 )
    return 0;

  if ( startptr )
    ptr = (char *)startptr;
  else if ( endptr )
    ptr = *endptr;
  else
    return 0;

  match_length = 0;
  cur_delimiter = 0;
  while ( *ptr ) {
    match_length++;
    if ( *ptr == delimiters[cur_delimiter] ) {
      cur_delimiter++;
      if ( cur_delimiter == delimiters_length ) {
        match_length -= cur_delimiter;
        break;
      }
    }
    else {
      if ( *ptr == delimiters[0] )
        cur_delimiter = 1;
      else
        cur_delimiter = 0;
    }
    ptr++;
  }

  if ( endptr )
    *endptr = ptr + 1;

  return match_length;
}

char *strnstr(const char *str, const char *needle, int len) {

  int i, last_i, needle_len;

  needle_len = strlen(needle);
  if ( needle_len == 0 )
    return (char *)str;

  last_i = len - needle_len;
  for ( i = 0; i < last_i; i++) {
    if ( str[0] == needle[0] && !strncmp(str, needle, needle_len) )
      return (char *)str;
    str++;
  }

  return NULL;
}

char *strstr_last(const char* const str1, const char* const str2) {
  /*
  Search string [str2] from right to left in string [str1].
  */

  char *strp;
  int len1, len2;

  len2 = strlen(str2);
  if ( len2 == 0 )
    return (char *)str1;

  len1 = strlen(str1);
  if ( len1 < len2 )
    return 0;

  strp = (char *)(str1 + len1 - len2);
  while ( strp != str1 ) {
    if ( *strp == *str2 && !strncmp(strp, str2, len2) )
      return strp;
    strp--;
  }
  if ( !strncmp(str1, str2, len2) )
    return (char *)str1;

  return NULL;
}

static char from_hex(const char ch) {

  if isdigit(ch)
    return ch - '0';
  if ( ch >= 'a' && ch <= 'f' )
    return ch - 'a' + 10;
  if ( ch >= 'A' && ch <= 'F' )
    return ch - 'A' + 10;
  return 0;
}

void decode_url(char *to, const char *from) {

  while ( *from ) {
    /* TODO: [1] and [2] may not exist */
    if ( *from == '%' && isxdigit(*(from + 1)) && isxdigit(*(from + 2)) ) {
      *to++ = from_hex(*(from + 1)) << 4 | from_hex(*(from + 2));
      from += 3;
      continue;
    }
    *to++ = *from++;
  }
  *to = 0;
}

int change_dots_to_underscore(char *const str) {

  char *strp;
  int dot_count;

  strp = str;
  dot_count = 0;
  while ( *strp ) {
    if ( *strp == '.' ) {
      *strp = '_';
      dot_count++;
    }
    else if ( !isalnum(*strp) && *strp != '-' ) {
      return -1;
    }
    strp++;
  }
  return dot_count;
}

int is_safe_filename(const char *const str) {

 char *strp;

  strp = (char *)str;
  while ( *strp ) {
    if ( !isprint(*strp) || *strp == '%' ) {
      return 0;
    }
    else if ( *strp == '.' ) {
      if ( *(strp + 1) == '.' )
        return 0;
    }
    else if ( *strp == '/' ) {
      if ( *(strp + 1) == '/' )
        return 0;
    }
    strp++;
  }
  return 1;
}

int ts_concatenate_path_filename(char *buf, int buflen, const char *path, const char *filename) {

  int path_length, filename_length, length;

  if ( filename == NULL )
    return -1;
  filename_length = strlen(filename);

  if ( path == NULL )
    path_length = 0;
  else
    path_length = strlen(path);

  length = 1; /* for NUL terminator */
  if ( path_length > 0 ) {
    length += path_length;
    if ( *(path + path_length - 1) != '/' )
      length++;
  }
  length += filename_length;

  if ( length > buflen )
    return -1;
  buf[0] = 0;

  if ( path_length > 0 ) {
    strcat(buf, path);
    if ( *(path + path_length - 1) != '/' )
      strcat(buf, "/");
  }
  strcat(buf, filename);

  return 0;
}
