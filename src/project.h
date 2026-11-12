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
#define REQUEST_READ_TIMEOUT_MS 1000
#define SEND_TIMEOUT_MS 1000
#define LINGER_TIMEOUT_MS 500

/* Connections handled at once by the single worker; more wait in the listen backlog. */
#define MAX_CONNECTIONS 10
/* Bytes of response body sent to one connection per loop round, so others get their turn. */
#define WRITE_BUDGET 262144
/* Pause before accepting again when out of file descriptors or memory. */
#define ACCEPT_RETRY_MS 100

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
