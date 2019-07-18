/*
 * x_ubuf.c
 */

#include	"x_ubuf.h"
#include	"x_debug.h"
#include	"x_errors_events.h"
#include	"x_syslog.h"
#include	"x_printf.h"

#include	"esp_vfs.h"

#include	<fcntl.h>

#define	debugFLAG				0xC000

#define	debugTRACK				(debugFLAG & 0x2000)
#define	debugPARAM				(debugFLAG & 0x4000)
#define	debugRESULT				(debugFLAG & 0x8000)

// #################################### PRIVATE structures #########################################

esp_vfs_t dev_ubuf = {
	.flags	= ESP_VFS_FLAG_DEFAULT,
	.write	= xUBufWrite,
	.read	= xUBufRead,
	.open	= xUBufOpen,
	.close	= xUBufClose,
	.ioctl	= xUBufIoctl,
} ;

ubuf_t	sUBuf[ubufMAX_OPEN] = { 0 } ;

static size_t	uBufSize	= ubufSIZE_DEFAULT ;

// ################################# Local/static functions ########################################

static int32_t	xUBufBlockAvail(ubuf_t * psUBuf) {
	if (psUBuf->Used == 0) {
		if (psUBuf->flags & O_NONBLOCK) {
			errno = EAGAIN ;
			return EOF ;
		} else {
			while (psUBuf->Used == 0) {
				vTaskDelay(10) ;
			}
		}
	}
	return erSUCCESS ;
}

static int32_t	xUBufBlockSpace(ubuf_t * psUBuf, size_t Size) {
	IF_myASSERT(debugPARAM, psUBuf->Size > Size) ;
	if (((psUBuf->Size - psUBuf->Used) < Size) &&		// if we have insufficient space
		(psUBuf->flags & O_TRUNC)) {					// we are supposed to TRUNCate
		Size -= psUBuf->Size - psUBuf->Used ;			// calculate space required
		psUBuf->IdxRD += Size ;							// adjust output/read index accordingly
		psUBuf->IdxRD %= psUBuf->Size ;					// then correct for wrap
		psUBuf->Used	-= Size ;						// adjust remaining character count
	} else if (psUBuf->Size == psUBuf->Used) {
	// not even 1 slot spare in buffer
		if (psUBuf->flags & O_NONBLOCK) {				// NOT supposed to block ?
			errno = EAGAIN ;							// set the error code
			return EOF ;								// and return
		} else {
			while (psUBuf->Size == psUBuf->Used) {		// wait for a single spot to open...
				vTaskDelay(10) ;						// loop waiting for sufficient space
			}
		}
	}
	return erSUCCESS ;
}

// ################################### Global/public functions #####################################

void	xUBufLock(ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf)) ;
	if (psUBuf->mux == NULL) {
		psUBuf->mux	= xSemaphoreCreateMutex() ;
	}
	xSemaphoreTake(psUBuf->mux, portMAX_DELAY) ;
}

void	xUBufUnLock(ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf->mux)) ;
	xSemaphoreGive(psUBuf->mux) ;
}

size_t	xUBufSetDefaultSize(size_t NewSize) {
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, NewSize, ubufSIZE_MAXIMUM, size_t)) ;
	return uBufSize = NewSize ;
}

int32_t	xUBufCreate(ubuf_t * psUBuf, char * pcBuf, size_t BufSize, size_t Used) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf) && ((pcBuf == NULL) || INRANGE_SRAM(pcBuf))) ;
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, BufSize, ubufSIZE_MAXIMUM, size_t) && Used <= BufSize) ;
	psUBuf->Size	= BufSize ;
	if (pcBuf != NULL) {
		psUBuf->pBuf	= pcBuf ;
		psUBuf->f_alloc = 0 ;
	} else {
		IF_myASSERT(debugPARAM, Used == 0) ;
		psUBuf->pBuf	= pvPortMalloc(BufSize) ;
		psUBuf->f_alloc = 1 ;
	}
	psUBuf->mux		= xSemaphoreCreateMutex() ;
	IF_myASSERT(debugRESULT, psUBuf->mux) ;
	if (Used == 0) {
		memset(psUBuf->pBuf, 0, psUBuf->Size) ;			// clear buffer ONLY if nothing to be used
		psUBuf->Used	= 0 ;
	} else {
		psUBuf->Used	= Used ;
	}
	psUBuf->IdxWR	= psUBuf->Used ;
	psUBuf->IdxRD	= 0 ;
	psUBuf->f_init	= 1 ;
	return psUBuf->Size ;
}

void	vUBufDestroy(ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf)) ;
	if (psUBuf->f_alloc == 1) {
		vPortFree(psUBuf->pBuf) ;
		psUBuf->pBuf 	= NULL ;
		psUBuf->Size 	= 0 ;
		psUBuf->f_init	= 0 ;
	}
	vSemaphoreDelete(psUBuf->mux) ;
}

void	vUBufReset(ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf)) ;
	psUBuf->IdxRD = psUBuf->IdxWR = psUBuf->Used = 0 ;
}

int32_t	xUBufGetC(ubuf_t * psUBuf) {
	if ((psUBuf->pBuf == NULL) || (psUBuf->Size == 0)) {
		errno = ENOMEM ;
		return erFAILURE ;
	}
	if (xUBufBlockAvail(psUBuf) != erSUCCESS) {
		return EOF ;
	}
	xUBufLock(psUBuf) ;
	int32_t cChr = *(psUBuf->pBuf + psUBuf->IdxRD++) ;
	psUBuf->IdxRD %= psUBuf->Size ;						// handle wrap
	if (--psUBuf->Used == 0) {							// buffer now empty
		psUBuf->IdxRD = psUBuf->IdxWR = 0 ;				// reset both In & Out indexes to start
	}
	IF_PRINT(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->Size, psUBuf->IdxWR, psUBuf->IdxRD, cChr) ;
	xUBufUnLock(psUBuf) ;
	return cChr ;
}

int32_t	xUBufPutC(ubuf_t * psUBuf, int32_t cChr) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf) && INRANGE_SRAM(psUBuf->pBuf) && (psUBuf->Size > 0)) ;
	if (xUBufBlockSpace(psUBuf, sizeof(char)) != erSUCCESS) {
		return EOF ;
	}
	xUBufLock(psUBuf) ;
	*(psUBuf->pBuf + psUBuf->IdxWR++) = cChr ;			// store character in buffer, adjust pointer
	if (psUBuf->IdxWR == psUBuf->Size) {				// and handle wrap
		psUBuf->IdxWR = 0 ;
	}
	++psUBuf->Used ;
	IF_PRINT(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->Size, psUBuf->IdxWR, psUBuf->IdxRD, cChr) ;
	xUBufUnLock(psUBuf) ;
	return cChr ;
}

char *	pcUBufGetS(char * pBuf, int32_t Number, ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(pBuf))
	char *	pTmp = pBuf ;
	while (Number > 1) {
		int32_t cChr = xUBufGetC(psUBuf) ;
		if (cChr == EOF) {								// EOF reached?
			*pTmp = CHR_NUL ;							// terminate buffer
			return NULL ;								// indicate EOF before NEWLINE
		}
		if (cChr == CHR_LF) {							// end of string reached ?
			*pTmp = cChr ;								// store the NEWLINE
			*pTmp = CHR_NUL ;							// terminate buffer
			return pBuf ;								// and return a valid state
		}
		if (cChr == CHR_CR) {
			continue ;
		}
		*pTmp++ = cChr ;								// store the character, adjust the pointer
		Number-- ;										// and update remaining chars to read
	}
// If we get here we have read (Number - 1) characters and still no NEWLINE
	*pTmp = CHR_NUL ;									// terminate buffer
	return pBuf ;										// and return a valid state
}

// ################################# ESP-IDF VFS compatible functions ##############################

void	vUBufInit(void) { ESP_ERROR_CHECK(esp_vfs_register(ubufDEV_PATH, &dev_ubuf, NULL)) ; }

int32_t	xUBufOpen(const char * pccPath, int flags, int Size) {
	IF_TRACK(debugTRACK, "path='%s'  flags=0x%x  Size=%d\n", pccPath, flags, Size) ;
	IF_myASSERT(debugPARAM, (*pccPath == CHR_FWDSLASH) && INRANGE(ubufSIZE_MINIMUM, Size, ubufSIZE_MAXIMUM, size_t)) ;
	int32_t fd = 0 ;
	do {
		if (sUBuf[fd].pBuf == NULL) {
			sUBuf[fd].pBuf	= pvPortMalloc(Size) ;
			sUBuf[fd].mux	= xSemaphoreCreateMutex() ;
			sUBuf[fd].flags	= flags ;
			sUBuf[fd].Size	= Size ;
			sUBuf[fd].IdxWR	= sUBuf[fd].IdxRD	= sUBuf[fd].Used	= 0 ;
			return fd ;
		} else {
			fd++ ;
		}
	} while(fd < ubufMAX_OPEN) ;
	errno = ENFILE ;
	return erFAILURE ;
}

int32_t	xUBufClose(int32_t fd) {
	if (INRANGE(0, fd, ubufMAX_OPEN-1, int32_t)) {
		ubuf_t * psUBuf = &sUBuf[fd] ;
		vPortFree(psUBuf->pBuf) ;
		vSemaphoreDelete(psUBuf->mux) ;
		memset(psUBuf, 0, sizeof(ubuf_t)) ;
		return erSUCCESS ;
	}
	errno = EBADF ;
	return erFAILURE ;
}

/**
 * xUBufRead() -
 */
ssize_t	xUBufRead(int fd, void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;
	if (xUBufBlockAvail(psUBuf) != erSUCCESS) {
		return EOF ;
	}
	ssize_t	count	= 0 ;
	xUBufLock(psUBuf) ;
	while((psUBuf->Used > 0) && (count < Size)) {
		*(char *)pBuf++ = *(psUBuf->pBuf + psUBuf->IdxRD++) ;
		--psUBuf->Used ;
		++count ;
		if (psUBuf->IdxRD == psUBuf->Size) {			// past the end?
			psUBuf->IdxRD = 0 ;						// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return count ;
}

/**
 * xUBufWrite() -
 */
ssize_t	xUBufWrite(int fd, const void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;
	if (xUBufBlockSpace(psUBuf, Size) != erSUCCESS) {
		return EOF ;
	}
	ssize_t	count	= 0 ;
	xUBufLock(psUBuf) ;
	while((psUBuf->Used < psUBuf->Size) && (count < Size)) {
		*(psUBuf->pBuf + psUBuf->IdxWR++) = *(const char *)pBuf++ ;
		++psUBuf->Used ;
		++count ;
		if (psUBuf->IdxWR == psUBuf->Size) {			// past the end?
			psUBuf->IdxWR = 0 ;							// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return count ;
}

int32_t	xUBufIoctl(int fd, int request, va_list vArgs) {
	ubuf_t ** ppsUBuf ;
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	switch(request) {
	case ioctlUBUF_I_PTR_CNTL:
		ppsUBuf = va_arg(vArgs, ubuf_t **) ;
		*ppsUBuf = &sUBuf[fd] ;
		break ;
	default:
		SL_ERR(debugAPPL_PLACE) ;
		return erFAILURE ;
	}
	return 1 ;
}

void	vUBufReport(ubuf_t * psUBuf) {
	printfx("p=%p  s=%d  u=%d  i=%d  o=%d  f=0x%x\n", psUBuf->pBuf, psUBuf->Size, psUBuf->Used, psUBuf->IdxWR, psUBuf->IdxRD, psUBuf->flags) ;
	if (psUBuf->Used) {
		printfx("%'!+b", psUBuf->Used, psUBuf->pBuf) ;
	}
}

// ################################## Diagnostic and testing functions #############################

#define	ubufTEST_SIZE	256

void	vUBufTest(void) {
	vUBufInit() ;
	int32_t Count, Result ;
	int32_t fd = open("/ubuf", O_RDWR | O_NONBLOCK) ;
	printfx("fd=%d\n", fd) ;
	// fill the buffer
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = write(fd, "a", 1) ;
		if (Result != 1) {
			printfx("write() FAILED with %d\n", Result) ;
		}
	}

	// check that it is full
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = write(fd, "A", 1) ;
	printfx("Result (%d) write() to FULL buffer =  %s\n", Result, (Result == 0) ? "Passed" : "Failed") ;

	// empty the buffer and check what is returned...
	char cBuf[4] = { 0 } ;
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = read(fd, cBuf, 1) ;
		if ((Result != 1) || (cBuf[0] != 'a')) {
			printfx("read() FAILED with %d & '%c\n", Result, cBuf[0]) ;
		}
	}

	// check that it is empty
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = read(fd, cBuf, 1) ;
	printfx("Result (%d) read() from EMPTY buffer = %s\n", Result, (Result == erFAILURE) ? "Passed" : "Failed") ;

	// Test printing to buffer
	for (Count = 0, Result = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result += dprintfx(fd, "%c", (Count % 10) + CHR_0) ;
	}
	printfx("dprintf() %s with %d expected %d\n", (Result == Count) ? "PASSED" : "FAILED" , Result, Count) ;

	// check that it is full
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = dprintfx(fd, "%c", CHR_A) ;
	printfx("Result (%d) dprintf() to FULL buffer (without O_TRUNC) =  %s\n", Result, (Result == erFAILURE) ? "Passed" : "Failed") ;

	Result = close(fd) ;
	printfx("Result (%d) close() buffer =  %s\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed") ;

	// Now test the O_TRUNC functionality
	size_t Size = xUBufSetDefaultSize(ubufTEST_SIZE) ;
	printfx("xUBufSetDefaultSize(%d) %s with %d\n", ubufTEST_SIZE, (Size == ubufTEST_SIZE) ? "PASSED" : "FAILED", Size) ;
	fd = open("/ubuf", O_RDWR | O_TRUNC) ;
	printfx("fd=%d\n", fd) ;
	// fill the buffer
	for (Count = 0; Count < ubufTEST_SIZE; ++Count) {
		Result = write(fd, "a", 1) ;
		if (Result != 1) {
			printfx("write() FAILED with %d\n", Result) ;
		}
	}
	Result = write(fd, "0123456789", 10) ;
	// check that it is full but with overwrite
	vUBufReport(&sUBuf[0]) ;

	Result = close(fd) ;
	printfx("Result (%d) close() buffer =  %s\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed") ;
}
