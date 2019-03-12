/*
 * Copyright 2014-18 AM Maree/KSS Technologies (Pty) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * x_buffers.h
 */

#pragma once

#include	<stdint.h>
#include	<stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// ############################## Heap and memory de/allocation related ############################

#define	configBUFSIZE_256						256
#define	configBUFSIZE_512						512
#define	configBUFSIZE_1K						1024

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
    char *          pBeg ;                          	// pointer to START of buffer
	char *			pEnd ;								// pointer to END of buffer (last space+1)
    char *			pWrite ;							// pointer to NEXT location to be written
    char *			pRead ;								// pointer to next position to read a char from
#if 	defined( __GNUC__ )
	volatile uint32_t	_flags ;
#elif 	defined( __TI_ARM__ )
	volatile uint32_t	flags ;
#endif
    size_t			xUsed ;
    size_t			xSize ;
	int32_t			handle ;
} buf_t ;

// #################################################################################################

void *	pvBufTake(size_t ) ;
int32_t	xBufGive(void * pvBuf) ;

int32_t	xBufReport(buf_t * psBuf) ;
void	vBufReset( buf_t * psBuf, size_t Used) ;
buf_t *	psBufOpen(void * pBuf, size_t Size, uint32_t flags, size_t Used) ;
int32_t	xBufClose(buf_t * psBuf) ;

size_t	xBufAvail(buf_t * psBuf) ;
size_t	xBufSpace(buf_t * psBuf) ;
int32_t	xBufPutC(int32_t cChr, buf_t * psBuf) ;
int32_t	xBufGetC(buf_t * psBuf) ;
int32_t xBufPeek(buf_t * psBuf) ;
char *	pcBufGetS(char * pcBuf, int32_t , buf_t * psBuf) ;

size_t	xBufWrite(void * pvBuf, size_t , size_t , buf_t * psBuf) ;
size_t	xBufRead(void * pvBuf, size_t , size_t , buf_t * psBuf) ;
int32_t	xBufSeek(buf_t * psBuf, int32_t , int32_t , int32_t ) ;
int32_t	xBufTell(buf_t * psBuf, int32_t ) ;
char *	pcBufTellPointer(buf_t * psBuf, int32_t flags) ;

int32_t	xBufPrintClose(buf_t * psBuf) ;
int32_t	xBufSyslogClose(buf_t * psBuf, uint32_t Prio) ;

#ifdef __cplusplus
}
#endif
