/*
 * x_ubuf.c
 * Copyright (c) 2016-22 Andre M. Maree / KSS Technologies (Pty) Ltd.
 */

#include <errno.h>

#include "x_ubuf.h"
#include "hal_config.h"
#include "printfx.h"
#include "syslog.h"
#include "systiming.h"
#include "x_errors_events.h"

#include "esp_vfs.h"

#define	debugFLAG					0xC000

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

static int xUBufBlockAvail(ubuf_t * psUB) {
	if ((psUB->pBuf == NULL) || (psUB->Size == 0)) {
		IF_myASSERT(debugTRACK, 0);
		errno = ENOMEM;
		return erFAILURE;
	}
	if (psUB->Used == 0) {
		if (psUB->flags & O_NONBLOCK) {
			errno = EAGAIN;
			return EOF;
		}
		while (psUB->Used == 0)
			vTaskDelay(2);
	}
	return erSUCCESS;
}

/**
 * MUST still check logic if Size requested is equal to or bigger than buffer size.
 * Also must do with a) empty and b) partial full buffers
 */
static ssize_t xUBufBlockSpace(ubuf_t * psUB, size_t Size) {
	IF_myASSERT(debugPARAM, psUB->Size > Size);
	if (Size > psUB->Size)								// in case requested size > buffer size
		Size = psUB->Size;								// limit requested size to buffer size
	ssize_t Avail = psUB->Size - psUB->Used;
	if (Avail >= Size)									// sufficient space ?
		return Size;
	// at this point, we do NOT have sufficient space available, must make space
	if (psUB->f_history || (psUB->flags & O_TRUNC)) {	// yes, supposed to TRUNCate ?
		xUBufLock(psUB);
		int Req = Size - Avail;
		psUB->IdxRD += Req;								// adjust output/read index accordingly
		psUB->IdxRD %= psUB->Size;						// correct for wrap
		psUB->Used -= Req;								// adjust remaining character count
		xUBufUnLock(psUB);
	} else if (psUB->flags & O_NONBLOCK) {				// non-blocking mode ?
		errno = EAGAIN;								// yes, set error code
		return Avail;									// and return actual space available
	} else {						// not truncating nor returning an error, WAIT !!!!
		do {
			if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
				vTaskDelay(2);							// loop waiting for sufficient space
			else
				xClockDelayMsec(2);
			Avail = psUB->Size - psUB->Used;
		} while (Avail < Size);							// wait for space to open...
	}
	return Size;
}

// ################################### Global/public functions #####################################

void xUBufLock(ubuf_t * psUB) {
	if (psUB->f_nolock == 0)
		xRtosSemaphoreTake(&psUB->mux, portMAX_DELAY);
}

void xUBufUnLock(ubuf_t * psUB) {
	if (psUB->f_nolock == 0)
		xRtosSemaphoreGive(&psUB->mux);
}

size_t xUBufSetDefaultSize(size_t NewSize) {
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, NewSize, ubufSIZE_MAXIMUM));
	return uBufSize = NewSize;
}

/**
 * @brief	Using the supplied uBUf structure, initialises the members as required
 * @param	psUB		structure to initialise
 * @param	pcBuf		preallocated buffer, if NULL will pvRtosMalloc
 * @param	BufSize		size of preallocated buffer, or size to be allocated
 * @param	Used		If preallocated buffer, portion already used
 * @return	Buffer size if successful, 0 if not.
 */
ubuf_t * psUBufCreate(ubuf_t * psUB, u8_t * pcBuf, size_t BufSize, size_t Used) {
	IF_myASSERT(debugPARAM, (psUB == NULL) || halCONFIG_inSRAM(psUB));
	IF_myASSERT(debugPARAM, (pcBuf == NULL) || halCONFIG_inSRAM(pcBuf));
	IF_myASSERT(debugPARAM, !(pcBuf == NULL && Used > 0));
	IF_myASSERT(debugPARAM, INRANGE(ubufSIZE_MINIMUM, BufSize, ubufSIZE_MAXIMUM) && Used <= BufSize);
	if (psUB != NULL) {
		psUB->f_struct = 0;
	} else {
		psUB = pvRtosMalloc(sizeof(ubuf_t));
		psUB->f_struct = 1;
	}
	if (pcBuf != NULL) {
		psUB->pBuf = pcBuf;
		psUB->f_alloc = 0;
	} else {
		psUB->pBuf = pvRtosMalloc(BufSize);
		psUB->f_alloc = 1;
	}
	psUB->mux = NULL;
	psUB->IdxWR = psUB->Used  = Used;
	psUB->IdxRD = 0;
	psUB->Size = BufSize;
	psUB->count = 0;
	psUB->f_nolock = 0;
	psUB->f_history = 0;
	if (Used == 0)
		memset(psUB->pBuf, 0, psUB->Size);			// clear buffer ONLY if nothing to be used
	psUB->f_init = 1;
	return psUB;
}

void vUBufDestroy(ubuf_t * psUB) {
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(psUB));
	if (psUB->mux)
		vRtosSemaphoreDelete(&psUB->mux);
	if (psUB->f_alloc) {
		vRtosFree(psUB->pBuf);
		psUB->pBuf = NULL;
		psUB->Size = 0;
		psUB->f_init = 0;
	}
	if (psUB->f_struct)
		vRtosFree(psUB);
}

void vUBufReset(ubuf_t * psUB) { psUB->IdxRD = psUB->IdxWR = psUB->Used = 0; }

int	xUBufAvail(ubuf_t * psUB) { return psUB->Used; }

int xUBufBlock(ubuf_t * psUB) {
	if (psUB->Used == 0)
		return 0;
	if (psUB->IdxRD >= psUB->IdxWR)
		return psUB->Size - psUB->IdxRD;
	return psUB->Used;
}

int	xUBufSpace(ubuf_t * psUB) { return psUB->Size - psUB->Used; }

/**
 * @brief	Empty the specified buffer using the handler supplied
 * 			Buffer will be emptied in 1 or 2 calls depending on state of pointers.
 * @return	0+ represent the number of bytes written
 * 			<0 representing an error code
 */
int xUBufEmptyBlock(ubuf_t * psUB, int (*hdlr)(u8_t *, ssize_t)) {
	if (psUB->Used == 0)
		return 0;
	if (hdlr == NULL)
		return erINV_PARA;
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
	IF_myASSERT(debugTRACK, psUB->Used==0 && psUB->IdxRD==0 && psUB->IdxWR==0);
	xUBufUnLock(psUB);
	return (iRV > 0 ) ? Total : iRV;
}

int	xUBufGetC(ubuf_t * psUB) {
	int iRV = xUBufBlockAvail(psUB);
	if (iRV != erSUCCESS)
		return iRV;
	xUBufLock(psUB);
	iRV = *(psUB->pBuf + psUB->IdxRD++);
	psUB->IdxRD %= psUB->Size;							// handle wrap
	if (--psUB->Used == 0)
		psUB->IdxRD = psUB->IdxWR = 0;					// reset In/Out indexes
	xUBufUnLock(psUB);
	IF_CP(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUB->Size, psUB->IdxWR, psUB->IdxRD, iRV);
	return iRV;
}

int	xUBufPutC(ubuf_t * psUB, int cChr) {
	int iRV = xUBufBlockSpace(psUB, sizeof(char));
	if (iRV != sizeof(char))
		return iRV;
	xUBufLock(psUB);
	*(psUB->pBuf + psUB->IdxWR++) = cChr;				// store character in buffer, adjust pointer
	psUB->IdxWR %= psUB->Size;							// handle wrap
	++psUB->Used;
	// ensure that the indexes are same when buffer is full
	IF_myASSERT(debugTRACK && (psUB->Used == psUB->Size), psUB->IdxRD == psUB->IdxWR);
	xUBufUnLock(psUB);
	IF_P(debugTRACK, "s=%d  i=%d  o=%d  cChr=%d", psUB->Size, psUB->IdxWR, psUB->IdxRD, cChr);
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

u8_t * pcUBufTellRead(ubuf_t * psUB) { return psUB->pBuf + psUB->IdxRD; }

u8_t * pcUBufTellWrite(ubuf_t * psUB)	{ return psUB->pBuf + psUB->IdxWR; }

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

void vUBufStepWrite(ubuf_t * psUB, int Step)	{
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

/**
 * @brief	copy previous (older) command added to buffer supplied
 * @return	number of characters copied
 */
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

/**
 * @brief	copy next (newer) command to buffer supplied
 * @return	number of characters copied
 */
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

/**
 * @brief	Add characters from buffer supplied to end of buffer
 * 			If insufficient free space, delete complete entries starting with oldest
 */
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
	IF_P(debugTRACK, "path='%s'  flags=0x%x  Size=%d", pccPath, flags, Size);
	IF_myASSERT(debugPARAM, (*pccPath == CHR_FWDSLASH) && INRANGE(ubufSIZE_MINIMUM, Size, ubufSIZE_MAXIMUM));
	int fd = 0;
	do {
		if (sUBuf[fd].pBuf == NULL) {
			sUBuf[fd].pBuf	= pvRtosMalloc(Size);
			sUBuf[fd].flags	= flags;
			sUBuf[fd].Size	= Size;
			sUBuf[fd].IdxWR	= sUBuf[fd].IdxRD	= sUBuf[fd].Used	= 0;
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
		vRtosFree(psUB->pBuf);
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
	if (OUTSIDE(0, fd, ubufMAX_OPEN-1) || (sUBuf[fd].pBuf == NULL) || (Size == 0)) {
		errno = EBADF;
		return erFAILURE;
	}
	ubuf_t * psUB = &sUBuf[fd];

	Size = xUBufBlockSpace(psUB, Size);
	if (Size < 1) {
		return EOF;
	}
	ssize_t	Count = 0;
	xUBufLock(psUB);
	while((psUB->Used < psUB->Size) && (Count < Size)) {
		*(psUB->pBuf + psUB->IdxWR++) = *(const char *)pBuf++;
		++psUB->Used;
		++Count;
		if (psUB->IdxWR == psUB->Size) {				// past the end?
			psUB->IdxWR = 0;							// yes, reset to start
		}
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

// ##################################### HISTORY use support #######################################



// ######################################## Reporting ##############################################

void vUBufReport(ubuf_t * psUB) {
	if (halCONFIG_inSRAM(psUB)) {
		printfx_lock();
		printfx_nolock("p=%p  s=%d  u=%d  Iw=%d  Ir=%d  mux=%p  f=0x%X",
			psUB->pBuf, psUB->Size, psUB->Used, psUB->IdxWR, psUB->IdxRD, psUB->mux, psUB->flags);
		printfx_nolock(" fI=%d fA=%d fS=%d fNL=%d fH=%d\r\n",
			psUB->f_init, psUB->f_alloc, psUB->f_struct, psUB->f_nolock, psUB->f_history);
		if (psUB->Used) {
			if (psUB->f_history) {
				u8_t * pNow = psUB->pBuf;
				u8_t u8Len;
				while (true) {
					u8Len = 0;
					while (*pNow) {
						if (u8Len == 0)
							printfx_nolock(" '");
						printfx_nolock("%c", *pNow);
						++pNow;
						if (pNow == psUB->pBuf + psUB->Size)
							pNow = psUB->pBuf;
						++u8Len;
					}
					if (u8Len > 0)
						printfx_nolock("'");
					++pNow;											// step over terminating '0'
					if (pNow == (psUB->pBuf + psUB->IdxWR))
						break;
				}
			} else {
				printfx_nolock("%!'+hhY\r\n", psUB->Used, psUB->pBuf);
			}
		}
		printfx_nolock(strCRLF);
		printfx_unlock();
	}
}

// ################################## Diagnostic and testing functions #############################

#define	ubufTEST_SIZE				256

void vUBufTest(void) {
	vUBufInit();
	int Count, Result;
	int fd = open("/ubuf", O_RDWR | O_NONBLOCK);
	printfx("fd=%d\r\n", fd);
	// fill the buffer
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = write(fd, "a", 1);
		if (Result != 1) {
			printfx("write() FAILED with %d\r\n", Result);
		}
	}

	// check that it is full
	vUBufReport(&sUBuf[0]);

	// Check that error is returned
	Result = write(fd, "A", 1);
	printfx("Result (%d) write() to FULL buffer =  %s\r\n", Result, (Result == 0) ? "Passed" : "Failed");

	// empty the buffer and check what is returned...
	char cBuf[4] = { 0 };
	for (Count = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result = read(fd, cBuf, 1);
		if ((Result != 1) || (cBuf[0] != CHR_a)) {
			printfx("read() FAILED with %d & '%c'\r\n", Result, cBuf[0]);
		}
	}

	// check that it is empty
	vUBufReport(&sUBuf[0]);

	// Check that error is returned
	Result = read(fd, cBuf, 1);
	printfx("Result (%d) read() from EMPTY buffer = %s\r\n", Result, (Result == erFAILURE) ? "Passed" : "Failed");

	// Test printing to buffer
	for (Count = 0, Result = 0; Count < ubufSIZE_DEFAULT; ++Count) {
		Result += dprintfx(fd, "%c", (Count % 10) + CHR_0);
	}
	printfx("dprintf() %s with %d expected %d\r\n", (Result == Count) ? "PASSED" : "FAILED" , Result, Count);

	// check that it is full
	vUBufReport(&sUBuf[0]);

	// Check that error is returned
	Result = dprintfx(fd, "%c", CHR_A);
	printfx("Result (%d) dprintf() to FULL buffer (without O_TRUNC) =  %s\r\n", Result, (Result == erFAILURE) ? "Passed" : "Failed");

	Result = close(fd);
	printfx("Result (%d) close() buffer =  %s\r\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed");

	// Now test the O_TRUNC functionality
	size_t Size = xUBufSetDefaultSize(ubufTEST_SIZE);
	printfx("xUBufSetDefaultSize(%d) %s with %d\r\n", ubufTEST_SIZE, (Size == ubufTEST_SIZE) ? "PASSED" : "FAILED", Size);
	fd = open("/ubuf", O_RDWR | O_TRUNC);
	printfx("fd=%d\r\n", fd);
	// fill the buffer
	for (Count = 0; Count < ubufTEST_SIZE; ++Count) {
		Result = write(fd, "a", 1);
		if (Result != 1) {
			printfx("write() FAILED with %d\r\n", Result);
		}
	}
	Result = write(fd, "0123456789", 10);
	// check that it is full but with overwrite
	vUBufReport(&sUBuf[0]);

	Result = close(fd);
	printfx("Result (%d) close() buffer =  %s\r\n", Result, (Result == erSUCCESS) ? "Passed" : "Failed");
}
