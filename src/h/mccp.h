/*****************************************************************************
 * DikuMUD (C) 1990, 1991 by:                                                *
 *   Sebastian Hammer, Michael Seifert, Hans Henrik Staefeldt, Tom Madsen,   *
 *   and Katja Nyboe.                                                        *
 *---------------------------------------------------------------------------*
 * MERC 2.1 (C) 1992, 1993 by:                                               *
 *   Michael Chastain, Michael Quan, and Mitchell Tse.                       *
 *---------------------------------------------------------------------------*
 * SMAUG 1.4 (C) 1994, 1995, 1996, 1998 by: Derek Snider.                    *
 *   Team: Thoric, Altrag, Blodkai, Narn, Haus, Scryn, Rennard, Swordbearer, *
 *         gorog, Grishnakh, Nivek, Tricops, and Fireblade.                  *
 *---------------------------------------------------------------------------*
 * SMAUG 1.7 FUSS by: Samson and others of the SMAUG community.              *
 *                    Their contributions are greatly appreciated.           *
 *---------------------------------------------------------------------------*
 * LoP (C) 2006 - 2013 by: the LoP team.                                     *
 *---------------------------------------------------------------------------*
 *                         Client Compression Module                         *
 *****************************************************************************/

#if defined(__has_include) && __has_include(<zlib.h>)
#include <zlib.h>
#else
#define Z_NULL 0
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_SYNC_FLUSH 2
#define Z_FINISH 4
typedef struct z_stream_s {
   unsigned char *next_in;
   unsigned int avail_in;
   unsigned char *next_out;
   unsigned int avail_out;
   void *zalloc;
   void *zfree;
   void *opaque;
} z_stream;
inline int deflateInit(z_stream *, int) { return -1; }
inline int deflate(z_stream *, int) { return -1; }
inline int deflateEnd(z_stream *) { return 0; }
#endif

#define TELOPT_COMPRESS2 86
#define COMPRESS_BUF_SIZE MSL

extern const unsigned char will_compress2_str[];
extern const unsigned char start_compress2_str[];

bool compressStart( DESCRIPTOR_DATA * d );
bool compressEnd( DESCRIPTOR_DATA * d );

typedef struct mccp_data MCCP;

struct mccp_data
{
   z_stream *out_compress;
   unsigned char *out_compress_buf;
};
