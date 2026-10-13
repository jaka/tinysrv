#ifndef _TINYSRV_PROJECT_H
#define _TINYSRV_PROJECT_H

/*
#define DEBUG
*/
#define FORK

#include "debug.h"

#define PROGRAM_NAME "tinysrv"
#define DEFAULT_PORT "8000"
#define CHAR_BUF_SIZE 8192
#define MAX_PATH_LENGTH 200
#define POST_DRAIN_TIMEOUT 1

#define TS_BACKLOG SOMAXCONN

typedef enum {
  SEND_CSS,
  SEND_FILE,
  SEND_GIF,
  SEND_HTML,
  SEND_ICO,
  SEND_JPG,
  SEND_WEBP,
  SEND_JS,
  SEND_PNG,
  SEND_SWF,
  SEND_TXT,
  SEND_NO_EXT,
  SEND_UNK_EXT
} response_enum;

#endif
