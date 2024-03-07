// x_buffers.h - Copyright (c) 2014-24 Andre M. Maree/KSS Technologies (Pty) Ltd.

#pragma once

#include "definitions.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ############################## Heap and memory de/allocation related ############################

#define	configBUFFERS_SIZE_MIN					64
#define	configBUFFERS_SIZE_MAX					32768
#define	configBUFFERS_MAX_OPEN					10

// ################################## Macros to simplify access ####################################

#define	BUF_PEEK(x)				*(x->pRead)
#define	BUF_POKE(x,y)			( *(x->pWrite) = y )

#define	BUF_NEXT_R(x)			( x->pRead++ )
#define	BUF_NEXT_W(x)			( x->pWrite++ )

#define	BUF_STEP_R(x,y)			( x->pRead += y )
#define	BUF_STEP_W(x,y)			( x->pWrite += y )

#define	BUF_TELL_R(x)			( x->pRead )
#define	BUF_TELL_W(x)			( x->pWrite )

#define	BUF_AVAIL(x)			( x->xUsed )
#define	BUF_SIZE(x)				( x->pWrite )

// ################################## Circular buffer control flags ################################

typedef struct buf_s {
    char * pBeg;                          	// pointer to START of buffer
	char * pEnd;								// pointer to END of buffer (last space+1)
    char * pWrite;							// pointer to NEXT location to be written
    char * pRead;								// pointer to next position to read a char from
#if defined( __GNUC__ )
	volatile uint32_t _flags;
#elif defined( __TI_ARM__ )
	volatile uint32_t flags;
#endif
    size_t xUsed;
    size_t xSize;
	int handle;
} buf_t;
DUMB_STATIC_ASSERT(sizeof(buf_t) == 32);

// #################################################################################################

void * pvBufTake(size_t );
int	xBufGive(void * pvBuf);
int	xBufReport(buf_t * psBuf);
void vBufReset( buf_t * psBuf, size_t Used);
buf_t *	psBufOpen(void * pBuf, size_t Size, uint32_t flags, size_t Used);
int	xBufClose(buf_t * psBuf);

size_t xBufAvail(buf_t * psBuf);
size_t xBufSpace(buf_t * psBuf);
int	xBufPutC(int cChr, buf_t * psBuf);
int	xBufGetC(buf_t * psBuf);
int xBufPeek(buf_t * psBuf);
char * pcBufGetS(char * pcBuf, int , buf_t * psBuf);

size_t xBufWrite(void * pvBuf, size_t , size_t , buf_t * psBuf);
size_t xBufRead(void * pvBuf, size_t , size_t , buf_t * psBuf);
int	xBufSeek(buf_t * psBuf, int , int , int );
int	xBufTell(buf_t * psBuf, int );
char * pcBufTellPointer(buf_t * psBuf, int flags);
int	xBufPrintClose(buf_t * psBuf);
int	xBufSyslogClose(buf_t * psBuf, uint32_t Prio);

#ifdef __cplusplus
}
#endif
