/*
 * x_ubuf.h
 */

#pragma	once

#include	<fcntl.h>
#include	<stdint.h>
#include	<stdarg.h>

#include	"FreeRTOS_Support.h"

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################

#define	ubufSIZE_MINIMUM			32
#define	ubufSIZE_DEFAULT			1024
#define	ubufSIZE_MAXIMUM			16384
#define	ubufMAX_OPEN				3
#define	ubufDEV_PATH				"/ubuf"

// ####################################### enumerations ############################################

enum { ioctlUBUF_UNDEFINED, ioctlUBUF_I_PTR_CNTL, ioctlUBUF_NUMBER };

// ####################################### structures  #############################################

typedef	struct ubuf_t {
	char * pBuf;
	SemaphoreHandle_t mux;
	u16_t flags;					// stdlib related flags
	union {
		struct  __attribute__((packed)) {
			u8_t	f_init	: 1;	// LSB
			u8_t	f_alloc	: 1;
			u8_t	f_nolock: 1;
			u16_t	_spare	: 13;	// MSB
		} ;
		u16_t	_flags;				// module flags
	};
	u16_t			Size;
	volatile u16_t	IdxWR;			// index to next space to WRITE to
	volatile u16_t	IdxRD;			// index to next char to be READ from
	volatile u16_t	Used;
} ubuf_t ;
DUMB_STATIC_ASSERT(sizeof(ubuf_t) == 20);

extern ubuf_t sUBuf[ubufMAX_OPEN] ;

// ################################### EXTERNAL FUNCTIONS ##########################################

void xUBufLock(ubuf_t * psUBuf) ;
void xUBufUnLock(ubuf_t * psUBuf) ;

int	xUBufAvail(ubuf_t * psUBuf) ;
int	xUBufSpace(ubuf_t * psUBuf) ;

char * pcUBufTellWrite(ubuf_t * psUBuf) ;
char * pcUBufTellRead(ubuf_t * psUBuf) ;

void vUBufStepWrite(ubuf_t * psUBuf, int Step) ;
void vUBufStepRead(ubuf_t * psUBuf, int Step) ;

size_t xUBufSetDefaultSize(size_t) ;
/**
 * Using the supplied uBUf structure, initialises the members as required
 * @param	psUBuf		structure to initialise
 * @param	pcBuf		preallocated buffer, if NULL will pvRtosMalloc
 * @param	BufSize		size of preallocated buffer, or size to be allocated
 * @param	Used		If preallocated buffer, portion already used
 * @return	Buffer size if successful, 0 if not.
 */
int	xUBufCreate(ubuf_t * psUBuf, char * pcBuf, size_t BufSize, size_t Used)  ;
void vUBufDestroy(ubuf_t *) ;
void vUBufReset(ubuf_t *) ;
int	xUBufGetC(ubuf_t *) ;
int	xUBufPutC(ubuf_t *, int) ;
char * pcUBufGetS(char *, int, ubuf_t *) ;

// VFS support functions
void vUBufInit(void) ;
int	xUBufOpen(const char *, int, int) ;
int	xUBufClose(int) ;
ssize_t	xUBufRead(int, void *, size_t) ;
ssize_t	xUBufWrite(int, const void *, size_t) ;
int	xUBufIoctl(int, int, va_list) ;

void vUBufReport(ubuf_t *) ;

#ifdef __cplusplus
}
#endif
