/*
 * x_ubuf.h
 */

#pragma	once

#include	"FreeRTOS_Support.h"

#include	<stdint.h>
#include	<stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################

#define	ubufMAX_OPEN				3
#define	ubufDEV_PATH				"/ubuf"

// ###################################### BUILD : CONFIG definitions ###############################

#define	ubufSIZE_MINIMUM			32
#define	ubufSIZE_DEFAULT			1024
#define	ubufSIZE_MAXIMUM			16384

// ####################################### enumerations ############################################

enum {
	ioctlUBUF_UNDEFINED,
	ioctlUBUF_I_PTR_CNTL,
	ioctlUBUF_NUMBER,
} ;

// ####################################### structures  #############################################

typedef	struct ubuf_s {
	char *				pBuf ;
	SemaphoreHandle_t	mux ;
	uint16_t			flags ;							// stdlib related flags
	union {
		struct {
			uint8_t		f_init	: 1 ;
			uint8_t		f_alloc	: 1 ;
		} ;
		uint16_t			_flags ;					// module flags
	} ;
	uint16_t			Size ;
	volatile uint16_t	IdxWR ;							// index to next space to WRITE to
	volatile uint16_t	IdxRD ;							// index to next char to be READ from
	volatile uint16_t	Used ;
} ubuf_t ;

extern ubuf_t	sUBuf[ubufMAX_OPEN] ;

// ################################### EXTERNAL FUNCTIONS ##########################################

void	xUBufLock(ubuf_t * psUBuf) ;
void	xUBufUnLock(ubuf_t * psUBuf) ;

inline int32_t	xUBufAvail(ubuf_t * psUBuf)		{ return psUBuf->Used ; }
inline int32_t	xUBufSpace(ubuf_t * psUBuf)		{ return psUBuf->Size - psUBuf->Used ; }

inline char * pcUBufTellWrite(ubuf_t * psUBuf)	{ return psUBuf->pBuf + psUBuf->IdxWR ; }
inline char * pcUBufTellRead(ubuf_t * psUBuf)	{ return psUBuf->pBuf + psUBuf->IdxRD ; }

inline void	vUBufStepWrite(ubuf_t * psUBuf, int32_t Step)	{ psUBuf->IdxWR += Step ; psUBuf->Used += Step ; }
inline void	vUBufStepRead(ubuf_t * psUBuf, int32_t Step)	{ psUBuf->IdxRD += Step ; psUBuf->Used -= Step ; }

size_t	xUBufSetDefaultSize(size_t) ;
int32_t	xUBufCreate(ubuf_t *, char *, size_t, size_t)  ;
void	vUBufDestroy(ubuf_t *) ;
void	vUBufReset(ubuf_t *) ;
int32_t	xUBufGetC(ubuf_t *) ;
int32_t	xUBufPutC(ubuf_t *, int32_t) ;
char *	pcUBufGetS(char *, int32_t, ubuf_t *) ;

// VFS support functions
void	vUBufInit(void) ;
int32_t	xUBufOpen(const char *, int, int) ;
int32_t	xUBufClose(int) ;
ssize_t	xUBufRead(int, void *, size_t) ;
ssize_t	xUBufWrite(int, const void *, size_t) ;
int32_t	xUBufIoctl(int, int, va_list) ;

void	vUBufReport(int, ubuf_t *) ;

#ifdef __cplusplus
}
#endif
