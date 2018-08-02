/*
 * x_ubuf.c
 */

#include	"x_ubuf.h"
#include	"x_debug.h"
#include	"x_errors_events.h"

#include	"esp_vfs.h"

#include	<fcntl.h>

// ##################################### DEBUG build macros ########################################

#define	debugFLAG					0x0001
#define	debugPARAM					(debugFLAG & 0x0001)
#define	debugTRACK					(debugFLAG & 0x0002)

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
	if (psUBuf->used == 0) {
		if (psUBuf->flags & O_NONBLOCK) {
			errno = EAGAIN ;
			return EOF ;
		} else {
			while (psUBuf->used == 0) {
				vTaskDelay(10) ;
			}
		}
	}
	return erSUCCESS ;
}

static int32_t	xUBufBlockSpace(ubuf_t * psUBuf, size_t Size) {
	IF_myASSERT(debugPARAM, psUBuf->size > Size) ;
	if (((psUBuf->size - psUBuf->used) < Size) && (psUBuf->flags & O_TRUNC)) {
	// we do not block if insufficient space & O_TRUNC is set, but discard the oldest character(s)
		Size -= psUBuf->size - psUBuf->used ;				// calculate space required
		psUBuf->IdxOut += Size ;							// adjust output/read index accordingly
		psUBuf->IdxOut %= psUBuf->size ;					// then correct for wrap
		psUBuf->used	-= Size ;							// adjust remaining character count
	} else if (psUBuf->size == psUBuf->used) {
	// not even 1 slot spare in buffer
		if (psUBuf->flags & O_NONBLOCK) {				// if we are supposed to block
			errno = EAGAIN ;							// set the error code
			return EOF ;								// and return
		} else {
			while (psUBuf->size == psUBuf->used) {		// wait for a single spot to open...
				vTaskDelay(10) ;						// loop waiting for sufficient space
			}
		}
	}
	return erSUCCESS ;
}

// ################################### Global/public functions #####################################

size_t	xUBufSetDefaultSize(size_t NewSize) {
	if (OUTSIDE(ubufSIZE_MINIMUM, NewSize, ubufSIZE_MAXIMUM, size_t)) {
		uBufSize = ubufSIZE_DEFAULT ;
	} else {
		uBufSize = NewSize ;
	}
	return uBufSize ;
}

int32_t	xUBufCreate(ubuf_t * psUBuf, size_t BufSize) {
	psUBuf->size	= OUTSIDE(ubufSIZE_MINIMUM, BufSize, ubufSIZE_MAXIMUM, size_t) ? ubufSIZE_DEFAULT : BufSize ;
	psUBuf->pBuf	= pvPortMalloc(psUBuf->size) ;
	psUBuf->mux		= xSemaphoreCreateMutex() ;
	psUBuf->used	= psUBuf->IdxIn	= psUBuf->IdxOut = 0 ;
	return erSUCCESS ;
}

int32_t	xUBufDestroy(ubuf_t * psUBuf) {
	vPortFree(psUBuf->pBuf) ;
	vSemaphoreDelete(psUBuf->mux) ;
	return erSUCCESS ;
}

int32_t	xUBufGotChar(ubuf_t * psUBuf) {	return psUBuf->used ; }

int32_t	xUBufGotSpace(ubuf_t * psUBuf) { return psUBuf->size - psUBuf->used ; }

int32_t	xUBufGetChar(ubuf_t * psUBuf) {
	if ((psUBuf->pBuf == NULL) || (psUBuf->size == 0)) {
		errno = ENOMEM ;
		return erFAILURE ;
	}
	if (xUBufBlockAvail(psUBuf) != erSUCCESS) {
		return EOF ;
	}
	xUBufLock(psUBuf) ;
	int32_t cChr = *(psUBuf->pBuf + psUBuf->IdxOut++) ;
	if (psUBuf->IdxOut == psUBuf->size) {				// past the end?
		psUBuf->IdxOut = 0 ;							// yes, reset to start
	}
	if (--psUBuf->used == 0) {							// buffer now empty
		psUBuf->IdxOut = psUBuf->IdxIn = 0 ;			// reset both In & Out indexes to start
	}
	IF_DEBUGPRINT_ERR(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->size, psUBuf->IdxIn, psUBuf->IdxOut, cChr) ;
	xUBufUnLock(psUBuf) ;
	return cChr ;
}

int32_t	xUBufPutChar(ubuf_t * psUBuf, int32_t cChr) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUBuf) && INRANGE_SRAM(psUBuf->pBuf) && (psUBuf->size > 0)) ;
	if (xUBufBlockSpace(psUBuf, sizeof(char)) != erSUCCESS) {
		return EOF ;
	}
	xUBufLock(psUBuf) ;
	*(psUBuf->pBuf + psUBuf->IdxIn++) = cChr ;			// store character in buffer, adjust pointer
	if (psUBuf->IdxIn == psUBuf->size) {				// and handle wrap
		psUBuf->IdxIn = 0 ;
	}
	++psUBuf->used ;
	IF_DEBUGPRINT_ERR(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUBuf->size, psUBuf->IdxIn, psUBuf->IdxOut, cChr) ;
	xUBufUnLock(psUBuf) ;
	return cChr ;
}

/**
 * ESP-IDF VFS compatible functions
 */
void	vUBufInit(void) { ESP_ERROR_CHECK(esp_vfs_register(ubufDEV_PATH, &dev_ubuf, NULL)) ; }

int32_t	xUBufOpen(const char * pccPath, int flags, int Size) {
	IF_TRACK_PRINT(debugTRACK, "path='%s'  flags=0x%x  Size=%d\n", pccPath, flags, Size) ;
	IF_myASSERT(debugPARAM, (*pccPath == CHR_FWDSLASH) && INRANGE(ubufSIZE_MINIMUM, Size, ubufSIZE_MAXIMUM, size_t)) ;
	int32_t fd = 0 ;
	do {
		if (sUBuf[fd].pBuf == NULL) {
			sUBuf[fd].pBuf	= pvPortMalloc(Size) ;
			sUBuf[fd].mux	= xSemaphoreCreateMutex() ;
			sUBuf[fd].flags	= flags ;
			sUBuf[fd].size	= Size ;
			sUBuf[fd].IdxIn	= sUBuf[fd].IdxOut	= sUBuf[fd].used	= 0 ;
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
ssize_t	xUBufRead(int fd, void * dst, size_t size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t) || (sUBuf[fd].pBuf == NULL) || (size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;
	if (xUBufBlockAvail(psUBuf) != erSUCCESS) {
		return EOF ;
	}
	char *	pBuf	= dst ;
	ssize_t	count	= 0 ;
	xUBufLock(psUBuf) ;
	while((psUBuf->used > 0) && (count < size)) {
		*pBuf++ = *(psUBuf->pBuf + psUBuf->IdxOut++) ;
		--psUBuf->used ;
		++count ;
		if (psUBuf->IdxOut == psUBuf->size) {			// past the end?
			psUBuf->IdxOut = 0 ;						// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return count ;
}

/**
 * xUBufWrite() -
 */
ssize_t	xUBufWrite(int fd, const void * data, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	ubuf_t * psUBuf = &sUBuf[fd] ;
	if (xUBufBlockSpace(psUBuf, Size) != erSUCCESS) {
		return EOF ;
	}
	const char * pBuf	= data ;
	ssize_t	count	= 0 ;
	xUBufLock(psUBuf) ;
	while((psUBuf->used < psUBuf->size) && (count < Size)) {
		*(psUBuf->pBuf + psUBuf->IdxIn++) = *pBuf++ ;
		++psUBuf->used ;
		++count ;
		if (psUBuf->IdxIn == psUBuf->size) {			// past the end?
			psUBuf->IdxIn = 0 ;							// yes, reset to start
		}
	}
	xUBufUnLock(psUBuf) ;
	return count ;
}

int32_t	xUBufIoctl(int fd, int request, va_list vArgs) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1, int32_t)) {
		errno = EBADF ;
		return erFAILURE ;
	}
	switch(request) {
	case ioctlUBUF_I_PTR_CNTL:
	{	ubuf_t ** ppsUBuf = 0 ;
		ppsUBuf = va_arg(vArgs, ubuf_t **) ;
		*ppsUBuf = &sUBuf[fd] ;
		break ;
	}
	default:
		break ;
	}
	return 1 ;
}

// ################################## Diagnostic and testing functions #############################

#define	ubufTEST_SIZE	256

void	vUBufReport(ubuf_t * psUBuf) {
	PRINT("p=%p  s=%d  u=%d  i=%d  o=%d  f=0x%x\n", psUBuf->pBuf, psUBuf->size, psUBuf->used, psUBuf->IdxIn, psUBuf->IdxOut, psUBuf->flags) ;
	if (psUBuf->used) {
		PRINT("%'!+b", psUBuf->size, psUBuf->pBuf) ;
	}
}

void	vUBufTest(void) {
	vUBufInit() ;
	int32_t Count, Result ;
	int32_t fd = open("/ubuf", O_RDWR | O_NONBLOCK) ;
	cprintf("fd=%d\n", fd) ;
	// fill the buffer
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = write(fd, "a", 1) ;
		if (Result != 1) {
			cprintf("write() FAILED with %d\n", Result) ;
		}
	}

	// check that it is full
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = write(fd, "A", 1) ;
	cprintf("Result (%d) write() to FULL buffer =  %s\n", Result, (Result == 0) ? "Passed" : "Failed") ;

	// empty the buffer and check what is returned...
	char cBuf[4] = { 0 } ;
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = read(fd, cBuf, 1) ;
		if ((Result != 1) || (cBuf[0] != 'a')) {
			cprintf("read() FAILED with %d & '%c\n", Result, cBuf[0]) ;
		}
	}

	// check that it is empty
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = read(fd, cBuf, 1) ;
	cprintf("Result (%d) read() from EMPTY buffer = %s\n", Result, (Result == erFAILURE) ? "Passed" : "Failed") ;

	// Test printing to buffer
	for (Count = 0, Result = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result += xdprintf(fd, "%c", (Count % 10) + CHR_0) ;
	}
	cprintf("dprintf() %s with %d expected %d\n", (Result == Count) ? "PASSED" : "FAILED" , Result, Count) ;

	// check that it is full
	vUBufReport(&sUBuf[0]) ;

	// Check that error is returned
	Result = xdprintf(fd, "%c", CHR_A) ;
	cprintf("Result (%d) dprintf() to FULL buffer (without O_TRUNC) =  %s\n", Result, (Result == erFAILURE) ? "Passed" : "Failed") ;

	Result = close(fd) ;
	cprintf("Result (%d) close() buffer =  %s\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed") ;

	// Now test the O_TRUNC functionality
	size_t Size = xUBufSetDefaultSize(ubufTEST_SIZE) ;
	cprintf("xUBufSetDefaultSize(%d) %s with %d\n", ubufTEST_SIZE, (Size == ubufTEST_SIZE) ? "PASSED" : "FAILED", Size) ;
	fd = open("/ubuf", O_RDWR | O_TRUNC) ;
	cprintf("fd=%d\n", fd) ;
	// fill the buffer
	for (Count = 0; Count < ubufTEST_SIZE; ++Count) {
		Result = write(fd, "a", 1) ;
		if (Result != 1) {
			cprintf("write() FAILED with %d\n", Result) ;
		}
	}
	Result = write(fd, "0123456789", 10) ;
	// check that it is full but with overwrite
	vUBufReport(&sUBuf[0]) ;

	Result = close(fd) ;
	cprintf("Result (%d) close() buffer =  %s\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed") ;
}

// ##################################### Minimalist buffers ########################################

#undef	debugFLAG
#define	debugFLAG					0x0001
#define	debugPARAM					(debugFLAG & 0x0001)

#define	pbufSIZE_MINIMUM			128
#define	pbufSIZE_DEFAULT			1024
#define	pbufSIZE_MAXIMUM			32768

int32_t	xUUBufGetC(uubuf_t * psUUBuf) {
	if (xUUBufUsed(psUUBuf) == 0) { return EOF ; }
	psUUBuf->Used-- ;										// adjust the Used counter
	return *(psUUBuf->pBuf + psUUBuf->Idx++) ;				// read character & adjust pointer
}

char *	pcUUBufGetS(char * pBuf, int32_t Number, uubuf_t * psUUBuf) {
	int32_t	cChr ;
	char *	pTmp = pBuf ;
	IF_myASSERT(debugPARAM, INRANGE_SRAM(pBuf))
	while (Number > 1) {
		cChr = xUUBufGetC(psUUBuf) ;
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

int32_t	xUUBufCreate(uubuf_t * psUUBuf, char * pcBuf, size_t BufSize, size_t Used) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUUBuf) && INRANGE(pbufSIZE_MINIMUM, BufSize, pbufSIZE_MAXIMUM, size_t) && (Used <= BufSize)) ;
	psUUBuf->Size	= BufSize ;
	psUUBuf->Idx	= 0 ;
	if (pcBuf) {
	// test pBuf against MEM not SRAM in case from flash..
		IF_myASSERT(debugPARAM, INRANGE_MEM(pcBuf)) ;
		psUUBuf->pBuf	= pcBuf ;
		psUUBuf->Used	= Used ;
		psUUBuf->Alloc	= 0 ;							// show memory as provided, NOT allocated
	} else {
		psUUBuf->pBuf	= pvPortMalloc(psUUBuf->Size) ;
		psUUBuf->Used	= 0 ;
		psUUBuf->Alloc	= psUUBuf->Size ;				// show memory as ALLOCATED
	}
	if (psUUBuf->Used == 0) {
		memset(psUUBuf->pBuf, 0, psUUBuf->Size) ;		// clear buffer ONLY if nothing to be used
	}
	return psUUBuf->Size ;
}

void	vUUBufDestroy(uubuf_t * psUUBuf) {
	IF_myASSERT(debugPARAM, INRANGE_SRAM(psUUBuf)) ;
	if (psUUBuf->Alloc) { vPortFree(psUUBuf->pBuf) ; }
}

void	vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj)	{
	if (Adj < 0) { psUUBuf->Idx -= Adj ; } else { psUUBuf->Idx += Adj ; }
	psUUBuf->Used += Adj;
}

void	vUUBufReport(int32_t Handle, uubuf_t * psUUBuf) {
	xdprintf(Handle, "P=%p  B=%p  I=%d  S=%d  U=%d  A=%d\n",
			psUUBuf, psUUBuf->pBuf, psUUBuf->Idx, psUUBuf->Size, psUUBuf->Used, psUUBuf->Alloc) ;
	if (psUUBuf->Used) {
		xdprintf(Handle, "%!'+b", psUUBuf->Used, psUUBuf->pBuf) ;
	}
}
