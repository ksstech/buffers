/*
 * x_ubuf.c
 */

#include	"x_ubuf.h"

#include	<errno.h>
#include	"esp_vfs.h"

#include	"hal_config.h"
#include 	"printfx.h"
#include	"syslog.h"
#include	"x_errors_events.h"

#define	debugFLAG					0xC000

#define	debugTIMING					(debugFLAG_GLOBAL & debugFLAG & 0x1000)
#define	debugTRACK					(debugFLAG_GLOBAL & debugFLAG & 0x2000)
#define	debugPARAM					(debugFLAG_GLOBAL & debugFLAG & 0x4000)
#define	debugRESULT					(debugFLAG_GLOBAL & debugFLAG & 0x8000)


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

static size_t uBufSize = ubufSIZE_DEFAULT ;

// ################################# Local/static functions ########################################

static int xUBufBlockAvail(ubuf_t * psUBuf) {
	if ((psUBuf->pBuf == NULL) || (psUBuf->Size == 0)) {
		IF_myASSERT(debugTRACK, 0);
		errno = ENOMEM ;
		return erFAILURE ;
	}
	if (psUBuf->Used == 0) {
		if (psUBuf->flags & O_NONBLOCK) {
			errno = EAGAIN ;
			return EOF ;
		}
		while (psUBuf->Used == 0)
			vTaskDelay(2);
	}
	return erSUCCESS ;
}

/**
 * MUST still check logic if Size requested is equal to of bigger than buffer size.
 * Also must do with a) empty and b) partial full buffers
 */
static ssize_t xUBufBlockSpace(ubuf_t * psUBuf, size_t Size) {
	IF_myASSERT(debugPARAM, psUBuf->Size > Size);
	ssize_t Avail = psUBuf->Size - psUBuf->Used;
	if (Avail >= Size)									// sufficient space ?
		return Size;

	if (psUBuf->flags & O_NONBLOCK) {					// non-blocking mode ?
			errno = EAGAIN ;							// yes, set error code
			return Avail;								// and return actual space available
	}

	if (Size > psUBuf->Size)							// in case size GT buffer size
		Size = psUBuf->Size;							// limit requested size to buffer size

	if (psUBuf->flags & O_TRUNC) {						// yes, supposed to TRUNCate ?
		xUBufLock(psUBuf);
		int Req = Size - Avail;
		psUBuf->IdxRD += Req;							// adjust output/read index accordingly
		psUBuf->IdxRD %= psUBuf->Size;					// correct for wrap
		psUBuf->Used -= Req;							// adjust remaining character count
		xUBufUnLock(psUBuf);

	} else {
		do {
			vTaskDelay(2);								// loop waiting for sufficient space
			Avail = psUBuf->Size - psUBuf->Used;
		} while (Avail < Size);							// wait for space to open...
	}
	return Size;
}

// ################################### Global/public functions #####################################

void xUBufLock(ubuf_t * psUBuf) {
	if (psUBuf->f_nolock == 0)
		xRtosSemaphoreTake(&psUBuf->mux, portMAX_DELAY);
}

void xUBufUnLock(ubuf_t * psUBuf) {
	if (psUBuf->f_nolock == 0)
		xRtosSemaphoreGive(&psUBuf->mux);
}

size_t xUBufSetDefaultSize(size_t NewSize) {
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, NewSize, ubufSIZE_MAXIMUM, size_t)) ;
	return uBufSize = NewSize ;
}

int	xUBufCreate(ubuf_t * psUBuf, char * pcBuf, size_t BufSize, size_t Used) {
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(psUBuf)) ;
	IF_myASSERT(debugPARAM, (pcBuf == NULL) || halCONFIG_inSRAM(pcBuf)) ;
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, BufSize, ubufSIZE_MAXIMUM, size_t) && Used <= BufSize) ;
	psUBuf->Size = BufSize;
	if (pcBuf != NULL) {
		psUBuf->pBuf = pcBuf;
		psUBuf->f_alloc = 0;
	} else if (Used == 0) {
		psUBuf->pBuf = pvRtosMalloc(BufSize) ;
		psUBuf->f_alloc = 1;
	} else {
		return 0;
	}
	if (Used == 0) {
		memset(psUBuf->pBuf, 0, psUBuf->Size);			// clear buffer ONLY if nothing to be used
	}
	psUBuf->IdxWR = psUBuf->Used  = Used;
	psUBuf->IdxRD = 0;
	psUBuf->f_init = 1;
	return psUBuf->Size;
}

void vUBufDestroy(ubuf_t * psUBuf) {
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(psUBuf)) ;
	if (psUBuf->f_alloc == 1) {
		free(psUBuf->pBuf);
		psUBuf->pBuf = NULL;
		psUBuf->Size = 0;
		psUBuf->f_init = 0;
	}
	vRtosSemaphoreDelete(&psUBuf->mux);
}

void vUBufReset(ubuf_t * psUBuf) { psUBuf->IdxRD = psUBuf->IdxWR = psUBuf->Used = 0; }

int	xUBufAvail(ubuf_t * psUBuf) { return psUBuf->Used ; }

int xUBufBlock(ubuf_t * psUBuf) {
	if (psUBuf->Used == 0)
		return 0;
	if (psUBuf->IdxRD >= psUBuf->IdxWR)
		return psUBuf->Size - psUBuf->IdxRD;
	return psUBuf->Used;
}

int	xUBufSpace(ubuf_t * psUBuf) { return psUBuf->Size - psUBuf->Used ; }

int xUBufEmptyBlock(ubuf_t * psUBuf, int (*hdlr)(char *, ssize_t)) {
	if (psUBuf->Used == 0)
		return 0;
	if (hdlr == NULL)
		return erINVALID_PARA;
	xUBufLock(psUBuf);
	int iRV = 0;
	ssize_t Size, Total = 0;
	if (psUBuf->IdxRD >= psUBuf->IdxWR) {
		Size = psUBuf->Size - psUBuf->IdxRD;
		iRV = hdlr(psUBuf->pBuf + psUBuf->IdxRD, Size);
		if (iRV > 0) {
			Total += Size;
			psUBuf->Used -= Size;							// decrease total available
			psUBuf->IdxRD = 0;								// reset read index
		}
	}
	if ((iRV >= 0) && psUBuf->Used) {
		iRV = hdlr(psUBuf->pBuf, psUBuf->Used);
		if (iRV > 0) {
			Total += psUBuf->Used;
			psUBuf->Used = 0;								// nothing left...
			psUBuf->IdxWR = 0;								// reset write index
		}
	}
	xUBufUnLock(psUBuf);
	return (iRV > 0 ) ? Total : iRV;
}

int	xUBufGetC(ubuf_t * psUBuf) {
	int iRV = xUBufBlockAvail(psUBuf);
	if (iRV != erSUCCESS)
		return iRV;

	xUBufLock(psUBuf);
	iRV = *(psUBuf->pBuf + psUBuf->IdxRD++);
	psUBuf->IdxRD %= psUBuf->Size;						// handle wrap
	if (--psUBuf->Used == 0)
		psUBuf->IdxRD = psUBuf->IdxWR = 0;				// reset In/Out indexes

	xUBufUnLock(psUBuf);
	IF_P(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->Size, psUBuf->IdxWR, psUBuf->IdxRD, iRV);
	return iRV;
}

int	xUBufPutC(ubuf_t * psUBuf, int cChr) {
	int iRV = xUBufBlockSpace(psUBuf, sizeof(char));
	if (iRV != sizeof(char)) {
		return iRV;
	}
	xUBufLock(psUBuf);
	*(psUBuf->pBuf + psUBuf->IdxWR++) = cChr;			// store character in buffer, adjust pointer
	psUBuf->IdxWR %= psUBuf->Size;						// handle wrap
	++psUBuf->Used;
	// ensure that the indexes are same when buffer is full
	IF_myASSERT(debugTRACK && (psUBuf->Used == psUBuf->Size), psUBuf->IdxRD == psUBuf->IdxWR);
	xUBufUnLock(psUBuf);
	IF_P(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->Size, psUBuf->IdxWR, psUBuf->IdxRD, cChr);
	return cChr;
}

char * pcUBufGetS(char * pBuf, int Number, ubuf_t * psUBuf) {
	char *	pTmp = pBuf ;
	while (Number > 1) {
		int cChr = xUBufGetC(psUBuf) ;
		if (cChr == EOF) {								// EOF reached?
			*pTmp = 0;
			return NULL;								// indicate EOF before NEWLINE
		}
		if (cChr != '\r')								// all except CR
			*pTmp++ = cChr;								// store character, adjust pointer
		--Number;										// update remaining chars to read
		if (cChr == '\n')								// end of string reached ?
			break;
	}
	*pTmp = 0;
	return pBuf ;										// and return a valid state
}

char * pcUBufTellRead(ubuf_t * psUBuf) { return psUBuf->pBuf + psUBuf->IdxRD ; }

char * pcUBufTellWrite(ubuf_t * psUBuf)	{ return psUBuf->pBuf + psUBuf->IdxWR ; }

void vUBufStepRead(ubuf_t * psUBuf, int Step) {
	IF_myASSERT(debugTRACK, Step > 0);
	xUBufLock(psUBuf);
	psUBuf->Used -= Step;
	if (psUBuf->Used) {
		psUBuf->IdxRD += Step;
		IF_myASSERT(debugTRACK, psUBuf->IdxRD <= psUBuf->Size);
		psUBuf->IdxRD %= psUBuf->Size;
	} else {
		psUBuf->IdxRD = psUBuf->IdxWR = 0;
	}
	xUBufUnLock(psUBuf);
}

void vUBufStepWrite(ubuf_t * psUBuf, int Step)	{
	IF_myASSERT(debugTRACK, Step > 0);
	xUBufLock(psUBuf);
	psUBuf->Used += Step;
	IF_myASSERT(debugTRACK, psUBuf->Used <= psUBuf->Size);	// cannot step outside
	psUBuf->IdxWR += Step;
	IF_myASSERT(debugTRACK, psUBuf->IdxWR <= psUBuf->Size);	// cannot step outside
	psUBuf->IdxWR %= psUBuf->Size;
	xUBufUnLock(psUBuf);
}

// ################################# ESP-IDF VFS compatible functions ##############################

void vUBufInit(void) { ESP_ERROR_CHECK(esp_vfs_register(ubufDEV_PATH, &dev_ubuf, NULL)) ; }

int	xUBufOpen(const char * pccPath, int flags, int Size) {
	IF_P(debugTRACK, "path='%s'  flags=0x%x  Size=%d", pccPath, flags, Size) ;
	IF_myASSERT(debugPARAM, (*pccPath == CHR_FWDSLASH) && INRANGE(ubufSIZE_MINIMUM, Size, ubufSIZE_MAXIMUM, size_t)) ;
	int fd = 0 ;
	do {
		if (sUBuf[fd].pBuf == NULL) {
			sUBuf[fd].pBuf	= pvRtosMalloc(Size) ;
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

int	xUBufClose(int fd) {
	if (INRANGE(0, fd, ubufMAX_OPEN-1, int)) {
		ubuf_t * psUBuf = &sUBuf[fd];
		vRtosFree(psUBuf->pBuf);
		vRtosSemaphoreDelete(&psUBuf->mux);
		memset(psUBuf, 0, sizeof(ubuf_t));
		return erSUCCESS;
	}
	errno = EBADF;
	return erFAILURE;
}

/**
 * xUBufRead() -
 */
ssize_t	xUBufRead(int fd, void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;
	int iRV = xUBufBlockAvail(psUBuf);
	if (iRV != erSUCCESS)
		return iRV;
	ssize_t	count = 0;
	xUBufLock(psUBuf) ;
	while((psUBuf->Used > 0) && (count < Size)) {
		*(char *)pBuf++ = *(psUBuf->pBuf + psUBuf->IdxRD++) ;
		--psUBuf->Used ;
		++count ;
		if (psUBuf->IdxRD == psUBuf->Size) {			// past the end?
			psUBuf->IdxRD = 0 ;							// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return count ;
}

ssize_t	xUBufWrite(int fd, const void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;

	Size = xUBufBlockSpace(psUBuf, Size);
	if (Size < 1) {
		return EOF ;
	}
	ssize_t	Count = 0;
	xUBufLock(psUBuf);
	while((psUBuf->Used < psUBuf->Size) && (Count < Size)) {
		*(psUBuf->pBuf + psUBuf->IdxWR++) = *(const char *)pBuf++ ;
		++psUBuf->Used ;
		++Count ;
		if (psUBuf->IdxWR == psUBuf->Size) {			// past the end?
			psUBuf->IdxWR = 0 ;							// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return Count ;
}

int	xUBufIoctl(int fd, int request, va_list vArgs) {
	ubuf_t ** ppsUBuf ;
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int)) {
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

void vUBufReport(ubuf_t * psUBuf) {
	cprintfx("p=%p  s=%d  u=%d  iW=%d  iR=%d", psUBuf->pBuf, psUBuf->Size, psUBuf->Used, psUBuf->IdxWR, psUBuf->IdxRD);
	cprintfx("  mux=%p  f=0x%X  f_init=%d  f_alloc=%d  f_nolock=%d\n", psUBuf->mux, psUBuf->flags, psUBuf->f_init, psUBuf->f_alloc, psUBuf->f_nolock);
	if (psUBuf->Used)
		cprintfx("%'!+B", psUBuf->Used, psUBuf->pBuf) ;
}

// ################################## Diagnostic and testing functions #############################

#define	ubufTEST_SIZE	256

void vUBufTest(void) {
	vUBufInit() ;
	int Count, Result ;
	int fd = open("/ubuf", O_RDWR | O_NONBLOCK) ;
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
