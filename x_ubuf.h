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
};
// ####################################### structures  #############################################

typedef	struct ubuf_s {
	uint8_t *			pBuf ;
	SemaphoreHandle_t	mux ;
	int32_t				flags ;
	uint16_t			size ;
	volatile uint16_t	IdxIn ;
	volatile uint16_t	IdxOut ;
	volatile uint16_t	used ;
} ubuf_t ;

extern ubuf_t	sUBuf[ubufMAX_OPEN] ;

// ################################### EXTERNAL FUNCTIONS ##########################################

inline void xUBufLock(ubuf_t * psUBuf)	{ xSemaphoreTake(psUBuf->mux, portMAX_DELAY) ; }
inline void xUBufUnLock(ubuf_t * psUBuf)	{ xSemaphoreGive(psUBuf->mux) ; }

size_t	xUBufSetDefaultSize(size_t BufSize) ;
int32_t	xUBufCreate(ubuf_t * psUBuf, size_t BufSize)  ;
int32_t	xUBufDestroy(ubuf_t * psUBuf) ;
int32_t	xUBufGotChar(ubuf_t * psUBuf) ;
int32_t	xUBufGetChar(ubuf_t * psUBuf) ;
int32_t	xUBufGotSpace(ubuf_t * psUBuf) ;
int32_t	xUBufPutChar(ubuf_t * psUBuf, int32_t cChr) ;
int32_t	xUBufStat(ubuf_t * psUBuf) ;

// VFS support functions
void	vUBufInit(void) ;
int32_t	xUBufOpen(const char *, int, int) ;
int32_t	xUBufClose(int32_t fd) ;
ssize_t	xUBufRead(int fd, void * dst, size_t size) ;
ssize_t	xUBufWrite(int fd, const void * data, size_t size) ;
int32_t	xUBufIoctl(int fd, int request, va_list vArgs) ;

void	vUBufReport(ubuf_t * psUBuf) ;

// ############################### Simple read OR write buffer structure ###########################

typedef	struct uubuf_s {
	char *				pBuf ;
	uint16_t			Idx ;
	uint16_t			Size ;
	uint16_t			Used ;
	uint16_t			Alloc ;
} uubuf_t ;

inline	size_t	xUUBufFree(uubuf_t * psUUBuf)			{ return psUUBuf->Size - psUUBuf->Used ; }
inline	size_t	xUUBufUsed(uubuf_t * psUUBuf)			{ return psUUBuf->Used ; }
inline	char *	pcUUBufPos(uubuf_t * psUUBuf)			{ return psUUBuf->pBuf + psUUBuf->Idx ; }

int32_t	xUUBufGetC(uubuf_t * psUUBuf) ;
char *	pcUUBufGetS(char * pBuf, int32_t Number, uubuf_t * psUUBuf) ;
int32_t	xUUBufCreate(uubuf_t * psUUBuf, char * pBuf, size_t BufSize, size_t Used) ;
void	vUUBufDestroy(uubuf_t * psUUBuf) ;
void	vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj) ;
void	vUUBufReport(int32_t Handle, uubuf_t * psUUBuf) ;

#ifdef __cplusplus
}
#endif
