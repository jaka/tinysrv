#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef DEBUG
 #include <stdio.h>
#endif

#ifdef DEBUG
 #define DEBUG_PRINT(fmt, args...) fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt "\n", \
  __FILE__, __LINE__, __func__, ##args)
#else
 #define DEBUG_PRINT(x, y...)
#endif

#endif
