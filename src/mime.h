#ifndef _TINYSRV_MIME_H
#define _TINYSRV_MIME_H

#include "project.h"
#include <stddef.h>

static const char httpnull_gif[] =
  "GIF89a" /* header */
  "\1\0\1\0" /* little endian width, height */
  "\x80" /* Global Colour Table flag */
  "\0" /* background colour */
  "\0" /* default pixel aspect ratio */
  "\1\1\1" /* RGB */
  "\0\0\0" /* RBG black */
  "!\xf9" /* Graphical Control Extension */
  "\4" /* 4 byte GCD data follow */
  "\1" /* there is transparent background color */
  "\0\0" /* delay for animation */
  "\0" /* transparent colour */
  "\0" /* end of GCE block */
  "," /* image descriptor */
  "\0\0\0\0" /* NW corner */
  "\1\0\1\0" /* height * width */
  "\0" /* no local color table */
  "\2" /* start of image LZW size */
  "\1" /* 1 byte of LZW encoded image data */
  "D" /* image data */
  "\0" /* end of image data */
  ";"; /* GIF file terminator */

static const char httpnull_png[] =
  "\x89"
  "PNG"
  "\r\n"
  "\x1a\n" /* EOF */
  "\0\0\0\x0d" /* 13 bytes length */
  "IHDR"
  "\0\0\0\1\0\0\0\1" /* width x height */
  "\x08" /* bit depth */
  "\x06" /* Truecolour with alpha */
  "\0\0\0" /* compression, filter, interlace */
  "\x1f\x15\xc4\x89" /* CRC */
  "\0\0\0\x0a" /* 10 bytes length */
  "IDAT"
  "\x78\x9c\x63\0\1\0\0\5\0\1"
  "\x0d\x0a\x2d\xb4" /* CRC */
  "\0\0\0\0" /* 0 length */
  "IEND"
  "\xae\x42\x60\x82"; /* CRC */

static const char httpnull_jpg[] =
  "\xff\xd8" /* SOI, Start Of Image */
  "\xff\xe0" /* APP0 */
  "\x00\x10" /* length of section 16 */
  "JFIF\0"
  "\x01\x01" /* version 1.1 */
  "\x01" /* pixel per inch */
  "\x00\x48" /* horizontal density 72 */
  "\x00\x48" /* vertical density 72 */
  "\x00\x00" /* size of thumbnail 0 x 0 */
  "\xff\xdb" /* DQT */
  "\x00\x43" /* length of section 3+64 */
  "\x00" /* 0 QT 8 bit */
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xff\xff\xff\xff\xff\xff\xff"
  "\xff\xc0" /* SOF */
  "\x00\x0b" /* length 11 */
  "\x08\x00\x01\x00\x01\x01\x01\x11\x00"
  "\xff\xc4" /* DHT Define Huffman Table */
  "\x00\x14" /* length 20 */
  "\x00\x01" /* DC table 1 */
  "\x00\x00\x00\x00\x00\x00\x00\x00"
  "\x00\x00\x00\x00\x00\x00\x00\x03"
  "\xff\xc4" /* DHT */
  "\x00\x14" /* length 20 */
  "\x10\x01" /* AC table 1 */
  "\x00\x00\x00\x00\x00\x00\x00\x00"
  "\x00\x00\x00\x00\x00\x00\x00\x00"
  "\xff\xda" /* SOS, Start of Scan */
  "\x00\x08" /* length 8 */
  "\x01" /* 1 component */
  "\x01\x00"
  "\x00\x3f\x00" /* Ss 0, Se 63, AhAl 0 */
  "\x37" /* image */
  "\xff\xd9"; /* EOI, End Of image */

static const char httpnull_swf[] =
  "FWS"
  "\x05" /* File version */
  "\x19\x00\x00\x00" /* litle endian size 16+9=25 */
  "\x30\x0A\x00\xA0" /* Frame size 1 x 1 */
  "\x00\x01" /* frame rate 1 fps */
  "\x01\x00" /* 1 frame */
  "\x43\x02" /* tag type is 9 = SetBackgroundColor block 3 bytes long */
  "\x00\x00\x00" /* black */
  "\x40\x00" /* tag type 1 = show frame */
  "\x00\x00"; /* tag type 0 - end file */

static const char httpnull_ico[] =
  "\x00\x00" /* reserved 0 */
  "\x01\x00" /* ico */
  "\x01\x00" /* 1 image */
  "\x01\x01\x00" /* 1 x 1 x >8bpp colour */
  "\x00" /* reserved 0 */
  "\x01\x00" /* 1 colour plane */
  "\x20\x00" /* 32 bits per pixel */
  "\x30\x00\x00\x00" /* size 48 bytes */
  "\x16\x00\x00\x00" /* start of image 22 bytes in */
  "\x28\x00\x00\x00" /* size of DIB header 40 bytes */
  "\x01\x00\x00\x00" /* width */
  "\x02\x00\x00\x00" /* height */
  "\x01\x00" /* colour planes */
  "\x20\x00" /* bits per pixel */
  "\x00\x00\x00\x00" /* no compression */
  "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
  "\x00\x00\x00\x00" /* end of header */
  "\x00\x00\x00\x00" /* Colour table */
  "\x00\x00\x00\x00" /* XOR B G R */
  "\x80\xF8\x9C\x41"; /* AND ? */

static const char httpnull_webp[] =
  "RIFF" /* header */
  "\x22\x00\x00\x00" /* file size */
  "WEBP" /* fourCC */
  "VP8\x20" /* chunk header */
  "\x16\x00\x00\x00"
  "\x30\x01\x00\x9d"
  "\x01\x2a\x01\x00\x01\x00\x0e\xc0"
  "\xfe\x25\xa4\x00\x03\x70\x00\x00"
  "\x00\x00";

/* ------------------------------------------------------------------------- */

typedef struct mime {
  /* File extension */
  const char *ext;
  /* File extension length */
  const unsigned int ext_len;
  /* Media type */
  const char *typestr;
  response_enum response_type;
  /* Default response string */
  const char *response;
  const unsigned int response_size;
} mime_t;

static const mime_t no_mime = { NULL, 0, "text/plain", SEND_NO_EXT, NULL, 0 };

static const mime_t default_mimes[] = {
  { "gif", 3, "image/gif", SEND_GIF, httpnull_gif, sizeof(httpnull_gif) - 1},
  { "png", 3, "image/png", SEND_PNG, httpnull_png, sizeof(httpnull_png) - 1},
  { "jp", 2, "image/jpeg", SEND_JPG, httpnull_jpg, sizeof(httpnull_jpg) - 1},
  { "webp", 4, "image/webp", SEND_WEBP, httpnull_webp, sizeof(httpnull_webp) - 1},
  { "swf", 3, "application/x-shockwave-flash", SEND_SWF, httpnull_swf, sizeof(httpnull_swf) - 1},
  { "ico", 3, "image/x-icon", SEND_ICO, httpnull_ico, sizeof(httpnull_ico) - 1},
  { "htm", 3, "text/html", SEND_HTML, NULL, 0},
  { "js", 2, "application/javascript", SEND_JS, NULL, 0},
  { "css", 3, "text/css", SEND_CSS, NULL, 0},
  { "txt", 3, "text/plain", SEND_TXT, NULL, 0},
  { NULL, 0, "text/html", SEND_UNK_EXT, NULL, 0}
};

#endif
