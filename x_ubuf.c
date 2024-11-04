// x_ubuf.c - Copyright (c) 2016-24 Andre M. Maree / KSS Technologies (Pty) Ltd.

#include "hal_platform.h"
#include "x_ubuf.h"

#include "hal_memory.h"
#include "hal_stdio.h"
#include "printfx.h"
#include "syslog.h"
#include "systiming.h"
#include "errors_events.h"

#include <errno.h>
#include "esp_vfs.h"

#define	debugFLAG					0xF000

#define	debugTIMING					(debugFLAG_GLOBAL & debugFLAG & 0x1000)
#define	debugTRACK					(debugFLAG_GLOBAL & debugFLAG & 0x2000)
#define	debugPARAM					(debugFLAG_GLOBAL & debugFLAG & 0x4000)
#define	debugRESULT					(debugFLAG_GLOBAL & debugFLAG & 0x8000)

// ##################################### MACRO definitions #########################################

#define	ubufMAX_OPEN				3
#define	ubufSIZE_MINIMUM			32
#define	ubufSIZE_MAXIMUM			16384
#define	ubufSIZE_DEFAULT			1024

// #################################### PRIVATE structures #########################################

static size_t uBufSize = ubufSIZE_DEFAULT;

// ################################# Local/static functions ########################################

static void xUBufLock(ubuf_t * psUB) {
	if (psUB->f_nolock == 0)
		xRtosSemaphoreTake(&psUB->mux, portMAX_DELAY); 
}

static void xUBufUnLock(ubuf_t * psUB) {
	if (psUB->f_nolock == 0)
		xRtosSemaphoreGive(&psUB->mux);
}

static int xUBufBlockAvail(ubuf_t * psUB) {
	if ((psUB->pBuf == NULL) || (psUB->Size == 0)) {
		IF_myASSERT(debugTRACK, 0);
		errno = ENOMEM;
		return erFAILURE;
	}
	if (psUB->Used == 0) {
		if (FF_STCHK(psUB, O_NONBLOCK)) {
			errno = EAGAIN; 
			return EOF;
		}
		while (psUB->Used == 0) {
			vTaskDelay(2);
		}
	}
	return erSUCCESS;
}

/**
 * MUST still check logic if Size requested is equal to or bigger than buffer size.
 * Also must do with a) empty and b) partial full buffers
 */
static ssize_t xUBufBlockSpace(ubuf_t * psUB, size_t Size) {
	IF_myASSERT(debugPARAM, Size <= psUB->Size);
	ssize_t Avail = psUB->Size - psUB->Used;
	if (Avail >= Size)
		return Size;									// sufficient space ?
	// at this point, we do NOT have sufficient space available, must make space
	if (psUB->f_history || FF_STCHK(psUB, O_TRUNC)) {	// yes, supposed to TRUNCate ?
		xUBufLock(psUB);
		int Req = Size - Avail;
		psUB->IdxRD += Req;								// adjust output/read index accordingly
		psUB->IdxRD %= psUB->Size;						// correct for wrap
		psUB->Used -= Req;								// adjust remaining character count
		xUBufUnLock(psUB);

	} else if (FF_STCHK(psUB, O_NONBLOCK)) {			// non-blocking mode ?
		FF_SET(psUB, FF_STATERR);
		errno = EAGAIN;									// yes, set error code
		return Avail;									// and return actual space available

	} else {											// block till available
		do {											// loop waiting for sufficient space
			if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
				vTaskDelay(2);							
			} else { 
				xClockDelayMsec(2);
			}
			xUBufLock(psUB);
			Avail = psUB->Size - psUB->Used;			// update available space
			xUBufUnLock(psUB);
		} while (Avail < Size);							// wait for space to open...
	}
	return Size;
}

// ################################### Global/public functions #####################################

size_t xUBufSetDefaultSize(size_t NewSize) {
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, NewSize, ubufSIZE_MAXIMUM));
	return uBufSize = NewSize;
}

ubuf_t * psUBufCreate(ubuf_t * psUB, u8_t * pcBuf, size_t BufSize, size_t Used) {
	IF_myASSERT(debugPARAM, (psUB == NULL) || halMemorySRAM(psUB));
	IF_myASSERT(debugPARAM, (pcBuf == NULL) || halMemorySRAM(pcBuf));
	IF_myASSERT(debugPARAM, !(pcBuf == NULL && Used > 0));
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, BufSize, ubufSIZE_MAXIMUM) && Used <= BufSize);
	if (psUB != NULL) {									// control structure supplied
		psUB->f_struct = 0;								// yes, flag as NOT allocated
	} else {
		psUB = malloc(sizeof(ubuf_t));					// no, allocate
		psUB->f_struct = 1;								// and flag as such
	}
	if (pcBuf != NULL) {								// buffer itself allocated
		psUB->pBuf = pcBuf;								// yes, save pointer into control structure
		psUB->f_alloc = 0;								// and flag as NOT allocated
	} else {
		psUB->pBuf = malloc(BufSize);					// no, allocate buffer of desired size
		psUB->f_alloc = 1;								// and flag as allocated
	}
	psUB->mux = NULL;
	psUB->IdxWR = psUB->Used  = Used;
	psUB->IdxRD = 0;
	psUB->Size = BufSize;
	psUB->count = 0;
	psUB->f_nolock = 0;
	psUB->f_history = 0;
	if (Used == 0)
		memset(psUB->pBuf, 0, psUB->Size);				// clear buffer ONLY if nothing to be used
	psUB->f_init = 1;
	return psUB;
}

void vUBufDestroy(ubuf_t * psUB) {
	IF_myASSERT(debugPARAM, halMemorySRAM(psUB));
	if (psUB->mux)
		vRtosSemaphoreDelete(&psUB->mux);
	if (psUB->f_alloc) {
		free(psUB->pBuf);
		psUB->f_alloc = 0;
		psUB->pBuf = NULL;
		psUB->Size = 0;
		psUB->f_init = 0;
	}
	if (psUB->f_struct) {
		free(psUB);
	}
}

void vUBufReset(ubuf_t * psUB) {
	xUBufLock(psUB);
	psUB->IdxRD = psUB->IdxWR = psUB->Used = 0; 
	xUBufUnLock(psUB);
}

int	xUBufGetUsed(ubuf_t * psUB) { return psUB->Used; }

int	xUBufGetSpace(ubuf_t * psUB) {
	xUBufLock(psUB);
	int iRV = psUB->Size - psUB->Used; 
	xUBufUnLock(psUB);
	return iRV;
}

int xUBufGetUsedBlock(ubuf_t * psUB) {
	int iRV;
	xUBufLock(psUB);
	iRV = (psUB->IdxRD >= psUB->IdxWR) ? (psUB->Size - psUB->IdxRD) : psUB->Used;
	xUBufUnLock(psUB);
	return iRV;
}

int xUBufEmptyBlock(ubuf_t * psUB, int (*hdlr)(u8_t *, ssize_t)) {
	if (psUB->Used == 0)			return 0;
	if (hdlr == NULL)				return erINV_PARA;
	xUBufLock(psUB);
	int iRV = 0;
	ssize_t Size, Total = 0;
	if (psUB->IdxRD >= psUB->IdxWR) {
		Size = psUB->Size - psUB->IdxRD;
		iRV = hdlr(psUB->pBuf + psUB->IdxRD, Size);
		if (iRV > 0) {
			Total += Size;
			psUB->Used -= Size;							// decrease total available
			psUB->IdxRD = 0;							// reset read index
		}
		IF_myASSERT(debugTRACK, iRV == Size);
	}
	if ((iRV >= 0) && psUB->Used) {
		iRV = hdlr(psUB->pBuf, psUB->Used);
		if (iRV > 0) {
			Total += psUB->Used;
			psUB->Used = 0;								// nothing left...
			psUB->IdxWR = 0;							// reset write index
		}
	}
	IF_myASSERT(debugTRACK, psUB->Used == 0);
	IF_myASSERT(debugTRACK, psUB->IdxRD == 0);
	IF_myASSERT(debugTRACK, psUB->IdxWR == 0);
	xUBufUnLock(psUB);
	return (iRV > 0 ) ? Total : iRV;
}

int	xUBufGetC(ubuf_t * psUB) {
	int iRV = xUBufBlockAvail(psUB);
	if (iRV != erSUCCESS)
		return iRV;
	xUBufLock(psUB);
	iRV = psUB->pBuf[psUB->IdxRD++];
	psUB->IdxRD %= psUB->Size;							// handle wrap
	if (--psUB->Used == 0)
		psUB->IdxRD = psUB->IdxWR = 0;					// reset In/Out indexes
	xUBufUnLock(psUB);
	return iRV;
}

int	xUBufPutC(ubuf_t * psUB, int cChr) {
	int iRV = xUBufBlockSpace(psUB, sizeof(char));
	if (iRV != sizeof(char))
		return iRV;
	xUBufLock(psUB);
	psUB->pBuf[psUB->IdxWR++] = cChr;					// store character in buffer, adjust pointer
	psUB->IdxWR %= psUB->Size;							// handle wrap
	++psUB->Used;
	xUBufUnLock(psUB);
	// ensure that the indexes are same when buffer is full
//	IF_CP (debugTRACK && (psUB->Used == psUB->Size) && (psUB->IdxRD != psUB->IdxWR), "ALERT!!! s=%d u=%d w=%d r=%d cChr=%d" strNL, psUB->Size, psUB->Used, psUB->IdxWR, psUB->IdxRD, cChr);
//	IF_myASSERT(debugTRACK && (psUB->Used == psUB->Size), psUB->IdxRD == psUB->IdxWR);
	return cChr;
}

char * pcUBufGetS(char * pBuf, int Number, ubuf_t * psUB) {
	char *	pTmp = pBuf;
	while (Number > 1) {
		int cChr = xUBufGetC(psUB);
		if (cChr == EOF) {								// EOF reached?
			*pTmp = 0;
			return NULL;								// indicate EOF before NEWLINE
		}
		if (cChr != CHR_CR)								// all except CR
			*pTmp++ = cChr;								// store character, adjust pointer
		--Number;										// update remaining chars to read
		if (cChr == CHR_LF || cChr == CHR_NUL)			// end of string reached ?
			break;
	}
	*pTmp = 0;
	return pBuf;										// and return a valid state
}

u8_t * pcUBufTellRead(ubuf_t * psUB) {
	xUBufLock(psUB);
	u8_t * pU8 = psUB->pBuf + psUB->IdxRD;
	xUBufUnLock(psUB);
	return pU8;
}

u8_t * pcUBufTellWrite(ubuf_t * psUB) {
	xUBufLock(psUB);
	u8_t * pU8 = psUB->pBuf + psUB->IdxWR;
	xUBufUnLock(psUB);
	return pU8;
}

void vUBufStepRead(ubuf_t * psUB, int Step) {
	IF_myASSERT(debugTRACK, Step > 0);
	if (psUB->f_history)
		return;
	xUBufLock(psUB);
	psUB->Used -= Step;
	if (psUB->Used) {
		psUB->IdxRD += Step;
		IF_myASSERT(debugTRACK, psUB->IdxRD <= psUB->Size);
		psUB->IdxRD %= psUB->Size;
	} else {
		psUB->IdxRD = psUB->IdxWR = 0;
	}
	xUBufUnLock(psUB);
}

void vUBufStepWrite(ubuf_t * psUB, int Step) {
	IF_myASSERT(debugTRACK, Step > 0);
	if (psUB->f_history)
		return;
	xUBufLock(psUB);
	psUB->Used += Step;
	IF_myASSERT(debugTRACK, psUB->Used <= psUB->Size);	// cannot step outside
	psUB->IdxWR += Step;
	IF_myASSERT(debugTRACK, psUB->IdxWR <= psUB->Size);	// cannot step outside
	psUB->IdxWR %= psUB->Size;
	xUBufUnLock(psUB);
}

// ################################# History buffer extensions #####################################

/* non CR	: add it to buffer with xUBufPutC()
 * CR		: add NUL to buffer with xUBufPutC(), set IdxRD = IdxWR
 * Cursor UP: set IdxRD back 1 entry, then copy entry to outside buffer.
 * 			  IdxRD left same place as before UP
 * Cursor DN: set IdxRD forward 1 entry, then copy entry to outside buffer
 *			  IdxRD left ....
 */
int xUBufStringCopy(ubuf_t * psUB, u8_t * pu8Buf, int xLen) {
	for (int xNow = 0; xNow < xLen; ++xNow) {
		*pu8Buf++ = psUB->pBuf[psUB->IdxRD++];
		psUB->IdxRD %= psUB->Size;
	}
	return xLen;
}

int xUBufStringNxt(ubuf_t * psUB, u8_t * pu8Buf, int Size) {
	IF_myASSERT(debugPARAM, psUB->f_history);
	// step back over NUL and then further back to chr before next NUL
	int xLen = 0;
	do {
		psUB->IdxRD = psUB->IdxRD ? (psUB->IdxRD - 1) : psUB->IdxWR;
		if ((psUB->pBuf[psUB->IdxRD] == CHR_NUL) && (xLen > 0)) {
			psUB->IdxRD = (psUB->IdxRD == (psUB->Size - 1)) ? 0 : (psUB->IdxRD + 1);
			break;
		}
		++xLen;
	} while (1);
	u16_t iTmp = psUB->IdxRD;							// save for reuse
	xLen = xUBufStringCopy(psUB, pu8Buf, xLen);
	psUB->IdxRD = iTmp;
	return xLen;
}

int xUBufStringPrv(ubuf_t * psUB, u8_t * pu8Buf, int Size) {
	IF_myASSERT(debugPARAM, psUB->f_history);
	int xLen = 0;
	if (psUB->Used == psUB->Size) {						// buffer is full then pointing at start of oldest/end
		while (psUB->pBuf[psUB->IdxRD] == CHR_NUL)
			psUB->IdxRD = (psUB->IdxRD == (psUB->Size - 1)) ? 0 : (psUB->IdxRD + 1);	// skip NUL's
		while (*(psUB->pBuf + psUB->IdxRD + xLen++) != CHR_NUL); //
	} else {											// buffer NOT full, no wrap yet
		if (psUB->IdxRD == psUB->IdxWR)					// at end of buffer ?
			psUB->IdxRD = 0;							// yes, set to start....
		while (psUB->pBuf[psUB->IdxRD + xLen++] != CHR_NUL); // calc length of string
	}
	return xUBufStringCopy(psUB, pu8Buf, xLen);
}

void vUBufStringAdd(ubuf_t * psUB, u8_t * pu8Buf, int Size) {
	IF_myASSERT(debugPARAM, psUB->f_history);
	int cChr;
	for(int i = 0; i <= Size; ++i) {					// include terminating '0' in copy...
		xUBufPutC(psUB, cChr = pu8Buf[i]);
		if (cChr == CHR_NUL)
			psUB->IdxRD = psUB->IdxWR;					// update IdxRD = IdxWR
	}
}

// ##################################### ESP-IDF VFS support #######################################

int xUBufOpen(const char *, int, int);
int xUBufClose(int);
ssize_t xUBufRead(int, void *, size_t);
ssize_t xUBufWrite(int, const void *, size_t);
int xUBufIoctl(int, int, va_list);

static const esp_vfs_t dev_ubuf = {
	.flags	= ESP_VFS_FLAG_DEFAULT,
	.write	= xUBufWrite,
	.lseek	= NULL,
	.read	= xUBufRead,
	.pread	= NULL,
	.pwrite	= NULL,
	.open	= xUBufOpen,
	.close	= xUBufClose,
	.fstat	= NULL,
	.fcntl	= NULL,
	.ioctl	= xUBufIoctl,
	.fsync	= NULL,
};

ubuf_t sUBuf[ubufMAX_OPEN] = { 0 };

void vUBufInit(void) { ESP_ERROR_CHECK(esp_vfs_register("/ubuf", &dev_ubuf, NULL)); }

int	xUBufOpen(const char * pccPath, int flags, int Size) {
//	CP("path='%s'  flags=0x%x  Size=%d", pccPath, flags, Size);
	IF_myASSERT(debugPARAM, (*pccPath == CHR_FWDSLASH) && INRANGE(ubufSIZE_MINIMUM, Size, ubufSIZE_MAXIMUM));
	int fd = 0;
	do {
		if (sUBuf[fd].pBuf == NULL) {
			sUBuf[fd].pBuf = malloc(Size);
			sUBuf[fd]._flags = flags;
			sUBuf[fd].Size = Size;
			sUBuf[fd].IdxWR	= sUBuf[fd].IdxRD = sUBuf[fd].Used = 0;
			return fd;
		} else {
			fd++;
		}
	} while(fd < ubufMAX_OPEN);
	errno = ENFILE;
	return erFAILURE;
}

int	xUBufClose(int fd) {
	if (INRANGE(0, fd, ubufMAX_OPEN-1)) {
		ubuf_t * psUB = &sUBuf[fd];
		free(psUB->pBuf);
		vRtosSemaphoreDelete(&psUB->mux);
		memset(psUB, 0, sizeof(ubuf_t));
		return erSUCCESS;
	}
	errno = EBADF;
	return erFAILURE;
}

/**
 * xUBufRead() -
 */
ssize_t	xUBufRead(int fd, void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF;
		return erFAILURE;
	}
	ubuf_t * psUB = &sUBuf[fd];
	int iRV = xUBufBlockAvail(psUB);
	if (iRV != erSUCCESS)
		return iRV;
	ssize_t	count = 0;
	xUBufLock(psUB);
	while((psUB->Used > 0) && (count < Size)) {
		*(char *)pBuf++ = *(psUB->pBuf + psUB->IdxRD++);
		--psUB->Used;
		++count;
		if (psUB->IdxRD == psUB->Size) {				// past the end?
			psUB->IdxRD = 0;							// yes, reset to start
		}
	}
	xUBufUnLock(psUB);
	return count;
}

ssize_t	xUBufWrite(int fd, const void * pBuf, size_t Size) {
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1) || !sUBuf[fd].pBuf || !Size) {
		errno = EBADF;
		return erFAILURE;
	}
	ubuf_t * psUB = &sUBuf[fd];

	ssize_t Avail = xUBufBlockSpace(psUB, Size);
	if (Avail < 1)
		return EOF;
	ssize_t	Count = 0;
	xUBufLock(psUB);
	while((psUB->Used < psUB->Size) && (Count < Avail)) {
		*(psUB->pBuf + psUB->IdxWR++) = *(const char *)pBuf++;
		++psUB->Used;
		++Count;
		if (psUB->IdxWR == psUB->Size)					// past the end?
			psUB->IdxWR = 0;							// yes, reset to start
	}
	xUBufUnLock(psUB);
	return Count;
}

int	xUBufIoctl(int fd, int request, va_list vArgs) {
	ubuf_t ** ppsUBuf;
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1)) {
		errno = EBADF;
		return erFAILURE;
	}
	switch(request) {
	case ioctlUBUF_I_PTR_CNTL:
		ppsUBuf = va_arg(vArgs, ubuf_t **);
		*ppsUBuf = &sUBuf[fd];
		break;
	default:
		SL_ERR(debugAPPL_PLACE);
		return erFAILURE;
	}
	return 1;
}

// ######################################## Reporting ##############################################

int vUBufReport(report_t * psR, ubuf_t * psUB) {
	int iRV = 0;
	if (halMemoryRAM(psUB)) {
		iRV += wprintfx(psR, "p=%p  s=%d  u=%d  Iw=%d  Ir=%d  mux=%p  f=0x%X",
			psUB->pBuf, psUB->Size, psUB->Used, psUB->IdxWR, psUB->IdxRD, psUB->mux, psUB->_flags);
		iRV += wprintfx(psR, " fI=%d fA=%d fS=%d fNL=%d fH=%d" strNL,
			psUB->f_init, psUB->f_alloc, psUB->f_struct, psUB->f_nolock, psUB->f_history);
		if (psUB->Used) {
			if (psUB->f_history) {
				u8_t * pNow = psUB->pBuf;
				u8_t u8Len;
				while (true) {
					u8Len = 0;
					while (*pNow) {
						if (u8Len == 0) iRV += wprintfx(psR, " '");
						iRV += wprintfx(psR, "%c", *pNow);
						++pNow;
						if (pNow == psUB->pBuf + psUB->Size) pNow = psUB->pBuf;
						++u8Len;
					}
					if (u8Len > 0) iRV += wprintfx(psR, "'");
					++pNow;											// step over terminating '0'
					if (pNow == (psUB->pBuf + psUB->IdxWR)) break;
				}
			} else {
				iRV += wprintfx(psR, "%!'+hhY" strNL, psUB->Used, psUB->pBuf);
			}
		}
		if (repFORM_TST(psR,aNL)) iRV += wprintfx(psR, strNL);
	}
	return iRV;
}

// ################################## Diagnostic and testing functions #############################

#define	ubufTEST_SIZE				256

void vUBufTest(void) {
	vUBufInit();
	int Count, Result;
	int fd = open("/ubuf", O_RDWR | O_NONBLOCK);
	wprintfx(NULL, "fd=%d" strNL, fd);
	// fill the buffer
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = write(fd, "a", 1);
		if (Result != 1) {
			wprintfx(NULL, "write() FAILED with %d" strNL, Result);
		}
	}

	// check that it is full
	vUBufReport(NULL, &sUBuf[0]);

	// Check that error is returned
	Result = write(fd, "A", 1);
	wprintfx(NULL, "Result (%d) write() to FULL buffer =  %s" strNL, Result, (Result == 0) ? "Passed" : "Failed");

	// empty the buffer and check what is returned...
	char cBuf[4] = { 0 };
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = read(fd, cBuf, 1);
		if ((Result != 1) || (cBuf[0] != CHR_a)) {
			wprintfx(NULL, "read() FAILED with %d & '%c'" strNL, Result, cBuf[0]);
		}
	}

	// check that it is empty
	vUBufReport(NULL, &sUBuf[0]);

	// Check that error is returned
	Result = read(fd, cBuf, 1);
	wprintfx(NULL, "Result (%d) read() from EMPTY buffer = %s" strNL, Result, (Result == erFAILURE) ? "Passed" : "Failed");

	// Test printing to buffer
	for (Count = 0, Result = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result += dprintfx(fd, "%c", (Count % 10) + CHR_0);
	}
	wprintfx(NULL, "dprintf() %s with %d expected %d" strNL, (Result == Count) ? "PASSED" : "FAILED" , Result, Count);

	// check that it is full
	vUBufReport(NULL, &sUBuf[0]);

	// Check that error is returned
	Result = dprintfx(fd, "%c", CHR_A);
	wprintfx(NULL, "Result (%d) dprintf() to FULL buffer (without O_TRUNC) =  %s" strNL, Result, (Result == erFAILURE) ? "Passed" : "Failed");

	Result = close(fd);
	wprintfx(NULL, "Result (%d) close() buffer =  %s" strNL, Result, (Result == erSUCCESS) ? "Passed" : "Failed");

	// Now test the O_TRUNC functionality
	size_t Size = xUBufSetDefaultSize(ubufTEST_SIZE);
	wprintfx(NULL, "xUBufSetDefaultSize(%d) %s with %d" strNL, ubufTEST_SIZE, (Size == ubufTEST_SIZE) ? "PASSED" : "FAILED", Size);
	fd = open("/ubuf", O_RDWR | O_TRUNC);
	wprintfx(NULL, "fd=%d" strNL, fd);
	// fill the buffer
	for (Count = 0; Count < ubufTEST_SIZE; ++Count) {
		Result = write(fd, "a", 1);
		if (Result != 1) {
			wprintfx(NULL, "write() FAILED with %d" strNL, Result);
		}
	}
	Result = write(fd, "0123456789", 10);
	// check that it is full but with overwrite
	vUBufReport(NULL, &sUBuf[0]);

	Result = close(fd);
	wprintfx(NULL, "Result (%d) close() buffer =  %s" strNL, Result, (Result == erSUCCESS) ? "Passed" : "Failed");
}
