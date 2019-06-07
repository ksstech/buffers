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
 * x_buffers.c - linear & circular buffer support routines
 *
 * Notes:
 * Change buffer support into simple file I/O
 * Enforce RO & WO modes for different files (circular/linear buffer) streams.
 */

#include	"x_buffers.h"

#include	"hal_config.h"
#include	"hal_nvic.h"

#include	"x_debug.h"
#include	"x_stdio.h"
#include	"x_utilities.h"
#include	"x_errors_events.h"
#include	"x_syslog.h"

#include	<string.h>

// ############################### BUILD: debug configuration options ##############################

#define	flagDEBUG							(0x0000)
#define	debugASSERT_POINTER					(flagDEBUG & 0x0001)
#define	debugASSERT_SIZE					(flagDEBUG & 0x0002)
#define	debugASSERT_CIRCULAR				(flagDEBUG & 0x0004)
#define	debugASSERT_MODE					(flagDEBUG & 0x0008)
#define	debugASSERT_RETURN					(flagDEBUG & 0x0010)
#define	debugSTRUCTURE						(flagDEBUG & 0x0020)
#define	debugTAKE_GIVE_PTR					(flagDEBUG & 0x0040)

// ################################### Private/local variables #####################################

buf_t		bufTable[configBUFFERS_MAX_OPEN] ;

#if		(ESP32_PLATFORM == 1)
	portMUX_TYPE	muxBuffers = { 0 } ;
#endif

// ############################## Heap and memory de/allocation related ############################

SemaphoreHandle_t	Buf256Lock , Buf512Lock, Buf1KLock ;
uint8_t	Buffer256[configBUFSIZE_256], Buffer512[configBUFSIZE_512], Buffer1K[configBUFSIZE_1K] ;

/**
 * pvBufTake()
 * @param BufSize
 */
void *	pvBufTake(size_t BufSize) {
	if (BufSize <= configBUFSIZE_256) {
		xUtilLockResource(&Buf256Lock, portMAX_DELAY) ;
		return &Buffer256[0] ;
	} else if (BufSize <= configBUFSIZE_512) {
		xUtilLockResource(&Buf512Lock, portMAX_DELAY) ;
		return &Buffer512[0] ;
	} else 	if (BufSize <= configBUFSIZE_1K) {
		xUtilLockResource(&Buf1KLock, portMAX_DELAY) ;
		return &Buffer1K[0] ;
	}
	IF_myASSERT(debugASSERT_SIZE, 0)
	return (void *) pdFAIL ;
}

/**
 * xBufGive()
 * @param pvBuf
 * @return
 */
int32_t	xBufGive(void * pvBuf) {
	if (pvBuf == &Buffer256[0]) {
		return xUtilUnlockResource(&Buf256Lock) ;
	} else if (pvBuf == &Buffer512[0]) {
		return xUtilUnlockResource(&Buf512Lock) ;
	} else 	if (pvBuf == &Buffer1K[0]) {
		return xUtilUnlockResource(&Buf1KLock) ;
	}
	IF_myASSERT(debugASSERT_POINTER, 0)
	return (BaseType_t) pdFAIL ;
}

// ################################# Private/Local support functions ###############################

/**
 * xBufCheck()
 * @param psBuf
 */
void	xBufCheck(buf_t * psBuf) {
	myASSERT(INRANGE_SRAM(psBuf)) ;
	myASSERT(INRANGE_SRAM(psBuf->pBeg)) ;
	myASSERT(INRANGE(configBUFFERS_SIZE_MIN, psBuf->xSize, configBUFFERS_SIZE_MAX, size_t)) ;
	myASSERT((psBuf->pEnd - psBuf->pBeg) == psBuf->xSize) ;
	myASSERT(psBuf->xUsed <= psBuf->xSize) ;
	myASSERT(INRANGE(psBuf->pBeg, psBuf->pRead, psBuf->pEnd, char *)) ;
	myASSERT(INRANGE(psBuf->pBeg, psBuf->pWrite, psBuf->pEnd, char *)) ;
}

/**
 * vBufIsrEntry()
 * @param psBuf
 */
static	void	vBufIsrEntry(buf_t * psBuf) {
	if (halNVIC_CalledFromISR() > 0) {					// if called from an ISR
		FF_SET(psBuf, FF_FROMISR) ;						// just set the flag
	} else {
#if		(ESP32_PLATFORM == 1)
		portENTER_CRITICAL(&muxBuffers) ;				// else disable interrupts
#else
		taskENTER_CRITICAL() ;							// else disable interrupts
#endif
	}
}

/**
 * vBufIsrExit()
 * @param psBuf
 */
static	void	vBufIsrExit(buf_t * psBuf) {
	if (FF_STCHK(psBuf, FF_FROMISR)) {					// if called from an ISR
		FF_UNSET(psBuf, FF_FROMISR) ;					// just clear the flag
	} else {
#if		(ESP32_PLATFORM == 1)
		portEXIT_CRITICAL(&muxBuffers) ;				// else re-enable interrupts
	#else
		taskEXIT_CRITICAL() ;							// else re-enable interrupts
	#endif
	}
}

/**
 * vBufTakePointer()
 * @return
 */
static	buf_t * vBufTakePointer( void ) {
int32_t	Index ;
#if		(ESP32_PLATFORM == 1)
	if ((muxBuffers.count == 0) && (muxBuffers.owner == 0)) {
		vPortCPUInitializeMutex(&muxBuffers) ;
	}
#endif
	for (Index = 0; Index < configBUFFERS_MAX_OPEN; Index++) {
		if (bufTable[Index].pBeg == 0) {
			IF_PRINT(debugTAKE_GIVE_PTR, "Take:%d\n", Index) ;
			return &bufTable[Index] ;
		}
	}
/* If we ASSERT() here something might be recursing hence eating up all structures.
 * Common cause is if we use an SL_ or IF_SL_ in the x_sockets module,
 * since this will call syslog() which will want to allocate a buffer,
 * which will call back here, and so we recurse to a crash...... */
	myASSERT(0) ;
	return NULL ;
}

/**
 * vBufGivePointer()
 * @param psBuf
 * @return
 */
static	int32_t	vBufGivePointer(buf_t * psBuf) {
int32_t	Index ;
	for (Index = 0; Index < configBUFFERS_MAX_OPEN; Index++) {
		if (psBuf == &bufTable[Index]) {
			psBuf->pBeg = 0 ;							/* Mark as closed/unused */
			IF_PRINT(debugTAKE_GIVE_PTR, "Give:%d\n", Index) ;
			return erSUCCESS ;
		}
	}
	myASSERT(0) ;
	return erFAILURE ;
}

int32_t	xBufCompact(buf_t * psBuf) {
	if (FF_STCHK(psBuf, FF_CIRCULAR) == 1 || FF_STCHK(psBuf, FF_MODEPACK) == 0) {
		return erFAILURE ;
	}
	if (psBuf->pRead > psBuf->pBeg) {					// yes, some empty space at start?
//		PRINT("Compacting") ;
		vBufIsrEntry(psBuf) ;
		memmove(psBuf->pBeg, psBuf->pRead, psBuf->xUsed) ;
		psBuf->pRead	= psBuf->pBeg ;					// reset read pointer to beginning
		psBuf->pWrite	= psBuf->pBeg + psBuf->xUsed ;	// recalc the write pointer
		vBufIsrExit(psBuf) ;
		memset(psBuf->pWrite, 0, psBuf->xSize - psBuf->xUsed) ;
	}
	return psBuf->xSize - psBuf->xUsed ;
}

// ################################### Public/Global functions #####################################

/**
 * xBufReport()
 * @param psBuf
 * @return
 */
int32_t	xBufReport(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	return xprintf("B=%p  E=%p  R=%p  W=%p  S=%d  U=%d",
								psBuf->pBeg,	psBuf->pEnd,
								psBuf->pRead,	psBuf->pWrite,
								psBuf->xSize,	psBuf->xUsed) ;
}

/**
 * vBufReset() - reset input/output pointers, allow to treat as a normal linear buffer
 * @param pCb		pointer to the buffer control structure
 * @param Used		amount of data in buffer, available to be read
 * @return			None
 */
void	vBufReset(buf_t * psBuf, size_t Used) {
	vBufIsrEntry(psBuf) ;
	psBuf->pRead	= psBuf->pBeg ;						// Setup READ pointers
	psBuf->pWrite	= psBuf->pBeg ;						// setup WRITE pointers
	psBuf->xUsed	= Used ;							// indicate (re)used space, if any
	FF_UNSET(psBuf, FF_UNGETC) ;						// and no ungetc'd character..
	vBufIsrExit(psBuf) ;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
}

/**
 * xBufReuse() populate the control structure fields
 * @param psBuf		pointer to already allocated buffer structure
 * @param pBuf		pointer to already allocated actual buffer space
 * @param Size		bytes of already allocated space
 * @param flags		defined in x_stdio.h (minimal implemented)
 * @param Used		data in buffer, available to be read
 * @return			erSUCCESS
 */
static int32_t	xBufReuse(buf_t * psBuf, char * pBuf, size_t Size, uint32_t flags, size_t Used) {
	IF_myASSERT(debugASSERT_POINTER, INRANGE_SRAM(psBuf))
	IF_myASSERT(debugASSERT_POINTER, INRANGE_MEM(pBuf))		// support RO buffers in FLASH
	IF_myASSERT(debugASSERT_SIZE, Used <= Size)
	vBufIsrEntry(psBuf) ;
	psBuf->pBeg		= pBuf ;
	psBuf->pEnd		= pBuf + Size ;						// calculate & save end
	psBuf->xSize	= Size ;
// Only some flags to be carried forward...
	FF_SET(psBuf, (flags & (FF_MODER | FF_MODEW | FF_MODERW | FF_MODEA | FF_MODEBIN | FF_CIRCULAR | FF_BUFFALOC))) ;
	vBufIsrExit(psBuf) ;
	vBufReset(psBuf, Used) ;
	return erSUCCESS ;
}

/**
 * psBufOpen() - create a buffer
 * @brief		allocate memory for the buffer and the control structure populate the control structure fields
 * @param pBuf		pointer to the buffer memory to use, 0 to create new
 * @param Size		buffer size to use or create
 * @param flags		based on flags as defined in x_stdio.h (minimal implemented)
 * @param Used		amount of data in buffer, available to be read
 * @return	pointer to the buffer handle or NULL if failed
 */
buf_t * psBufOpen(void * pBuf, size_t Size, uint32_t flags, size_t Used) {
	if ((pBuf == NULL) && (INRANGE(configBUFFERS_SIZE_MIN, Size, configBUFFERS_SIZE_MAX, size_t) == false)) {
		myASSERT(0) ;
		return pvFAILURE ;
	}
	IF_myASSERT(debugASSERT_SIZE, Used <= Size)
	buf_t *	psBuf = vBufTakePointer() ;							// get a free table entry
	if (psBuf != NULL) {								// unused entry found?
		vBufIsrEntry(psBuf) ;
		if (pBuf == 0) {
			pBuf 	= pvPortMalloc(Size) ;				// allocate memory for buffer
			flags	|= FF_BUFFALOC ;					// make sure flag is SET !!
			memset(pBuf, 0, Size) ;
			Used	= 0 ;								// cannot have used something in a new buffer
		} else {
			flags	&= ~FF_BUFFALOC ;					// make sure flag is CLEAR !!
		}
		vBufIsrExit(psBuf) ;
		xBufReuse(psBuf, pBuf, Size, flags, Used)	;	// setup
	}
	return psBuf ;
}

/**
 * xBufClose() - delete a buffer
 * @brief	deallocate the memory for the buffer and the control structure
 * @param	psBuf	pointer to the buffer control structure
 * @return	SUCCESS if deleted with error, FAILURE otherwise
 */
int32_t	xBufClose(buf_t * psBuf) {
int32_t	iRetVal ;
char *	pTmp ;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	vBufIsrEntry(psBuf) ;
	pTmp = psBuf->pBeg ;								// save pointer for later use..
	iRetVal = vBufGivePointer(psBuf) ;
	if ((iRetVal == erSUCCESS) && FF_STCHK(psBuf, FF_BUFFALOC)) {
#if 	defined( __GNUC__ )
		psBuf->_flags = 0 ;
#elif 	defined( __TI_ARM__ )
		psBuf->flags = 0 ;
#endif
		vPortFree(pTmp) ;								// return buffer
	}
	vBufIsrExit(psBuf) ;
	return iRetVal ;
}

/**
 * xBufAvail() - get the number of characters in the buffer
 * @param psBuf	pointer to the buffer control structure
 * @return		Number of characters
 */
size_t	xBufAvail(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	return psBuf->xUsed ;
}

/**
 * xBufSpace() - get the empty slots in buffer
 * @brief		automatically pack the buffer to free up maximum contiguous space
 * @param psBuf	pointer to the buffer control structure
 * @return		Number of empty slots
 */
size_t	xBufSpace(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	if (FF_STCHK(psBuf, FF_MODEPACK)) {
		xBufCompact(psBuf) ;
	}
	return psBuf->xSize - psBuf->xUsed ;				// compacted space available
}

/**
 * xBufPutC() - write a character to the buffer
 * @brief		buffer and control structure MUST have been created prior
 * @param psBuf pointer to buffer control structure
 * @param cChr	the char to be written
 * @return		cChr if the char was written or EOF if discarded/overwritten
 */
int32_t	xBufPutC(int32_t cChr, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	int32_t	iRetVal ;
	if ((cChr == CHR_LF) && (FF_STCHK(psBuf, FF_MODEBIN) == 0)) {
		iRetVal = xBufPutC(CHR_CR, psBuf) ;
		if (iRetVal == EOF) {
			return iRetVal ;
		}
	}
	if (psBuf->xSize > psBuf->xUsed) {
		vBufIsrEntry(psBuf) ;
		*psBuf->pWrite++ = cChr ;							// Firstly store char in buffer
		psBuf->xUsed++ ;									// & adjust the Used counter
		if (psBuf->pWrite == psBuf->pEnd) { 				// Last character written in last slot &
			psBuf->pWrite = psBuf->pBeg ;					// yes, wrap write pointer to start
		}
		vBufIsrExit(psBuf) ;
		iRetVal = cChr ;
	} else {
		iRetVal = EOF ;
	}
	return iRetVal ;
}

/**
 * xBufGetC() - read a character from the buffer
 * @brief		buffer and control structure MUST have been created prior
 * @param psBuf	pointer to buffer control structure
 * @return		if successful the character read else EOF
 */
int32_t	xBufGetC(buf_t * psBuf) {
	if (xBufAvail(psBuf) == 0) {
		return EOF ;
	}
	vBufIsrEntry(psBuf) ;
	int32_t cChr = *psBuf->pRead++ ;							// read character & adjust pointer
	psBuf->xUsed-- ;									// & adjust the Used counter
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {					// Circular buffer ...
		if (psBuf->pRead == psBuf->pEnd) {				// and at end of buffer?
			psBuf->pRead = psBuf->pBeg ;				// yes, wrap to start
		}
	} else {											// linear buffer ?
		if (psBuf->xUsed == 0) {						// Handle pointer reset if empty
			psBuf->pWrite	= psBuf->pBeg ;
			psBuf->pRead	= psBuf->pBeg ;
		}
	}
	vBufIsrExit(psBuf) ;
	return cChr ;
}

/**
 * xBufPeek()
 * @param psBuf
 * @return
 */
int32_t xBufPeek(buf_t * psBuf) {
	if (xBufAvail(psBuf) == 0) {
		return EOF ;
	}
	return *psBuf->pRead ;								// read character
}

/**
 * pcBufGetS()
 * @param pBuf
 * @param Number
 * @param psBuf
 * @return
 */
char *	pcBufGetS(char * pBuf, int32_t Number, buf_t * psBuf) {
int32_t	cChr ;
char *	pTmp = pBuf ;
	IF_myASSERT(debugASSERT_POINTER, INRANGE_SRAM(pBuf))
	while (Number > 1) {
		cChr = xBufGetC(psBuf) ;
		if (cChr == EOF) {								// EOF reached?
			*pTmp = CHR_NUL ;							// terminate buffer
			return NULL ;								// indicate EOF before NEWLINE
		}
		if (cChr == CHR_LF) {							// end of string reached ?
			*pTmp = cChr ;								// store the NEWLINE
			*pTmp = CHR_NUL ;							// terminate buffer
			return pBuf ;								// and return a valid state
		}
		if ((cChr == CHR_CR) && (FF_STCHK(psBuf, FF_MODEBIN) == false)) {
			continue ;
		}
		*pTmp++ = cChr ;								// store the character, adjust the pointer
		Number-- ;										// and update remaining chars to read
	}
// If we get here we have read (Number - 1) characters and still no NEWLINE
	*pTmp = CHR_NUL ;									// terminate buffer
	return pBuf ;										// and return a valid state
}

/**
 * xBufWrite() - add 1 or more structures to the packet payload
 * @brief
 * @param pvBuf	pointer to new payload data
 * @param Size	of structure/unit to be added
 * @param Count	of items of the structure/unit to be added
 * @param psBuf	pointer to the buffer structure
 * @return		number of bytes allocated to buffer or an error code
 */
size_t	xBufWrite(void * pvBuf, size_t Size, size_t Count, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	IF_myASSERT(debugASSERT_POINTER, INRANGE_SRAM(pvBuf))
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {
		IF_myASSERT(debugASSERT_CIRCULAR, 0)
		return 0 ;										// indicate nothing written
	}

	Count *= Size ;										// calculate requested number of BYTES
	if (Count > (psBuf->pEnd - psBuf->pWrite)) {		// write size bigger than available to end?
		xBufCompact(psBuf) ;							// compact up, if possible
		Count = psBuf->pEnd - psBuf->pWrite ;			// then adjust...
	}
	vBufIsrEntry(psBuf) ;
	memcpy(psBuf->pWrite, pvBuf, Count) ;				// move contents across
	psBuf->pWrite	+= Count ;							// update the payload pointers and length counters
	psBuf->xUsed	+= Count ;
	vBufIsrExit(psBuf) ;
	return Count ;
}

/**
 * xBufRead()
 * @param pvBuf
 * @param Size
 * @param xLen
 * @param psBuf
 * @return
 */
size_t	xBufRead(void * pvBuf, size_t Size, size_t Count, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	IF_myASSERT(debugASSERT_POINTER, INRANGE_SRAM(pvBuf))
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {
		IF_myASSERT(debugASSERT_CIRCULAR, 0)
		return 0 ;										// indicate nothing read
	}
	if (Size == 0 || Count == 0) {
		IF_myASSERT(debugASSERT_SIZE, 0)
		return 0 ;
	}

	Count *= Size ;										// calculate requested number of BYTES
	if (Count > psBuf->xUsed) {							// If more requested than available,
		Count = psBuf->xUsed ;							// then adjust...
	}
	vBufIsrEntry(psBuf) ;
	memcpy(pvBuf, psBuf->pRead, Count) ;				// move contents across
	psBuf->pRead	+= Count ;							// update READ pointer for next
	psBuf->xUsed	-= Count ;							// adjust remaining count
	if (psBuf->xUsed == 0) {
		psBuf->pRead = psBuf->pWrite = psBuf->pBeg ;	// reset all to start
	}
	vBufIsrExit(psBuf) ;
	return Count ;
}

/**
 * xBufSeek()
 * @param psBuf
 * @param Offset
 * @param whence
 * @param flags
 * @return
 */
int32_t	xBufSeek(buf_t * psBuf, int32_t Offset, int32_t whence, int32_t flags) {
char * pTmp ;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {					// working on CIRCULAR buffer
		IF_myASSERT(debugASSERT_CIRCULAR, 0) ;
		return erFAILURE ;								// yes, abort
	}
	IF_CPRINT(debugSTRUCTURE, "[Seek 1] B=%p R=%p W=%p S=%d U=%d\n", psBuf->pBeg, psBuf->pRead, psBuf->pWrite, psBuf->xSize, psBuf->xUsed) ;

	vBufIsrEntry(psBuf) ;
	if (flags & FF_MODER) {
		pTmp =  (whence == SEEK_SET)	? psBuf->pBeg + Offset :
				(whence == SEEK_CUR)	? psBuf->pRead + Offset :
				(whence == SEEK_END)	? psBuf->pEnd + Offset : psBuf->pRead ;
		if (pTmp < psBuf->pBeg) {
			myASSERT(0) ;
			pTmp = psBuf->pBeg ;
		} else if (pTmp > psBuf->pEnd) {
			myASSERT(0) ;
			pTmp = psBuf->pEnd ;
		}
		psBuf->pRead = pTmp ;
	}
	if (flags & FF_MODEW) {
		pTmp =  (whence == SEEK_SET)	? psBuf->pBeg + Offset :
				(whence == SEEK_CUR)	? psBuf->pWrite + Offset :
				(whence == SEEK_END)	? psBuf->pEnd + Offset : psBuf->pWrite ;
		if (pTmp < psBuf->pBeg) {			// seek pos BEFORE start of buffer?
			myASSERT(0)	;					// yes !!!
			pTmp = psBuf->pBeg ;
		} else if (pTmp > psBuf->pEnd) {	// seek pos BEYOND end of buffer?
			myASSERT(0)	;					// yes,
			pTmp = psBuf->pEnd ;
		}
		psBuf->pWrite = pTmp ;
	}
	psBuf->xUsed = psBuf->pWrite - psBuf->pRead ;
	vBufIsrExit(psBuf) ;

	IF_myASSERT(debugASSERT_SIZE, (psBuf->xUsed <= psBuf->xSize)) ;
	IF_CPRINT(debugSTRUCTURE, "[Seek 2] B=%p R=%p W=%p S=%d U=%u\n", psBuf->pBeg, psBuf->pRead, psBuf->pWrite, psBuf->xSize, psBuf->xUsed) ;
	return erSUCCESS ;
}

/**
 * xBufTell() - return index of read or write pointer into the buffer
 * @param psBuf
 * @param flags
 * @return
 */
int32_t	xBufTell(buf_t * psBuf, int32_t flags) {
int32_t		iRetVal = erFAILURE;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {				// working on circular buffer
		IF_myASSERT(debugASSERT_CIRCULAR, 0)
		return erFAILURE ;							// yes, abort..
	}

	// Can only ask for MODER or MODEW not both or MODERW
	if (((flags & FF_MODER) && (flags & FF_MODEW)) || (flags & FF_MODERW)) {
		IF_myASSERT(debugASSERT_MODE, 0)
		return erFAILURE ;
	}
	vBufIsrEntry(psBuf) ;
	if (flags & FF_MODER) {
		iRetVal =  psBuf->pRead - psBuf->pBeg ;
	} else if (FF_STCHK(psBuf, FF_MODEW)) {
		iRetVal = psBuf->pWrite - psBuf->pBeg ;
	}
	vBufIsrExit(psBuf) ;
	return iRetVal ;
}

/**
 * pcBufTellPointer()
 * @param psBuf
 * @param flags
 * @return
 */
char *	pcBufTellPointer(buf_t * psBuf, int32_t flags) {
char * pcRetVal = (char *) erFAILURE ;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {				// working on circular buffer
		IF_myASSERT(debugASSERT_CIRCULAR, 0)
		return pcRetVal ;
	}

	// Can only ask for MODER or MODEW not both nor MODERW
	if (((flags & FF_MODER) && (flags & FF_MODEW)) || (flags & FF_MODERW)) {
		IF_myASSERT(debugASSERT_MODE, 0)
		return pcRetVal ;
	}
	if (flags & FF_MODER) {
		pcRetVal = psBuf->pRead ;
	} else if (flags & FF_MODEW) {
		pcRetVal = psBuf->pWrite ;
	}
	IF_myASSERT(debugASSERT_RETURN, INRANGE_SRAM(pcRetVal))
	return pcRetVal ;
}

/**
 * xBufPrintClose() - output buffer contents to the console and close the buffer
 * @param psBuf		pointer to the managed buffer to be printed
 * @return			number of characters printed
 */
int32_t	xBufPrintClose(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	IF_myASSERT(debugASSERT_SIZE, psBuf->xUsed > 0)
	int32_t iRetVal = xnprintf(psBuf->xUsed, psBuf->pRead) ;
	xBufClose(psBuf) ;
	return iRetVal ;
}

/**
 * xBufSyslogClose() - output buffer contents to syslog  hosts and close the buffer
 * @param psBuf		pointer to the managed buffer to be printed
 * @param Prio		Syslog priority to be used
 * @return			status of the buffer close event
 */
int32_t	xBufSyslogClose(buf_t * psBuf, uint32_t Prio) {
int32_t	iRetVal ;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf) ;
	IF_myASSERT(debugASSERT_SIZE, psBuf->xUsed > 0)
	xSyslog(Prio, psBuf->pRead, "") ;
	iRetVal = xBufClose(psBuf) ;
	return iRetVal ;
}

#define	bufSIZE		100
#define	bufSTEP		10

void	vBufUnitTest(void) {
	int32_t	iRV ;
	buf_t * psBuf = psBufOpen(0, bufSIZE, FF_MODER|FF_MODEW, 0) ;
	for(int32_t a = 0; a < bufSIZE; a += bufSTEP) {
		for(int32_t b = 0; b < bufSTEP; b++) {
			iRV = xBufPutC(b + '0', psBuf) ;
			if (iRV != (b + '0')) 	PRINT("Failed")  ;
		}
	}
	// buffer should be full
	if (xBufAvail(psBuf) != bufSIZE) 											PRINT("Failed")  ;
	if (xBufSpace(psBuf) != 0) 													PRINT("Failed")  ;
	if (pcBufTellPointer(psBuf, FF_MODER) != psBuf->pBeg) 						PRINT("Failed")  ;
	// because we are using the xUsed value to track space, pWrite should be wrapped around
	if (pcBufTellPointer(psBuf, FF_MODEW) != psBuf->pRead) 						PRINT("Failed")  ;

	// make sure that we get an error if we write another char...
	if (xBufPutC('Z', psBuf) != EOF) 											PRINT("Failed")  ;

	// now read the buffer empty and verify the contents
	for(int32_t a = 0; a < bufSIZE; a++) {
		if (xBufGetC(psBuf) != ('0' + (a % bufSTEP)))							PRINT("Failed")  ;
	}

	// make sure we get an EOF error if we try read another char
	if (xBufGetC(psBuf) != EOF)													PRINT("Failed")  ;

	// at this stage (empty) the pointers should both automatically be reset to the start
	if ((psBuf->pRead != psBuf->pWrite) || (psBuf->pRead != psBuf->pBeg))		PRINT("Failed")  ;

	// seek the write pointer to the end, effectively making all content available again.
	if (xBufSeek(psBuf, bufSIZE, SEEK_SET, FF_MODEW) != erSUCCESS)				PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				PRINT("Failed A=%d - B=%d", xBufAvail(psBuf), xBufSpace(psBuf))  ;

	// rewind the write pointer, make effectively empty
	if (xBufSeek(psBuf, -bufSIZE, SEEK_END, FF_MODEW) != erSUCCESS)				PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				PRINT("Failed")  ;

	// move write pointer to middle
	if (xBufSeek(psBuf, bufSIZE/2, SEEK_SET, FF_MODEW) != erSUCCESS)			PRINT("Failed")  ;
	if (xBufAvail(psBuf) != xBufSpace(psBuf))									PRINT("Failed")  ;

	// move read pointer to middle
	if (xBufSeek(psBuf, bufSIZE/2, SEEK_SET, FF_MODER) != erSUCCESS)			PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				PRINT("Failed")  ;

	// move both read & write pointers simultaneously to start
	if (xBufSeek(psBuf, -bufSIZE/2, SEEK_CUR, FF_MODER|FF_MODEW) != erSUCCESS)	PRINT("Failed")  ;
	if ((psBuf->pBeg != psBuf->pRead) && (psBuf->pBeg != psBuf->pWrite))		PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				PRINT("Failed")  ;

	if (xBufSeek(psBuf, bufSIZE, SEEK_SET, FF_MODEW) != erSUCCESS)				PRINT("Failed")  ;
	PRINT("Avail=100\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;
	xBufSeek(psBuf, bufSIZE / 2, SEEK_SET, FF_MODER) ;
	PRINT("Avail=50%\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;
	xBufSeek(psBuf, -(bufSIZE / 4), SEEK_CUR, FF_MODER) ;
	PRINT("Avail=75%\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;

	char cBuffer[50] ;
	xBufSeek(psBuf, 0, SEEK_SET, FF_MODER) ;
	xBufSeek(psBuf, 0, SEEK_END, FF_MODEW) ;
	// read pointer should be at start and write pointer at the end.
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				PRINT("Failed A=%d - B=%d", xBufAvail(psBuf), xBufSpace(psBuf))  ;

	// read first 25 characters at start of buffer, no compacting should have happened
	if (xBufRead(cBuffer, 5, 5, psBuf) != 25)									PRINT("Failed")  ;
	PRINT("Avail=75\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;
	PRINT("cBuffer=25\n%!'+b", 25, cBuffer) ;
	if ((xBufAvail(psBuf) != 75) || (xBufSpace(psBuf) != 25))					PRINT("Failed")  ;

	// try to write 25 chars just read, should fail since FF_MODEPACK not enabled
	if (xBufWrite(cBuffer, 5, 5, psBuf) != 0)									PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != 75) || (xBufSpace(psBuf) != 25))					PRINT("Failed")  ;
	PRINT("Avail=75\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;

	FF_SET(psBuf, FF_MODEPACK) ;
	// try to write 25 chars just read, should be placed at end after compacting...
	if (xBufWrite(cBuffer, 5, 5, psBuf) != 25)									PRINT("Failed")  ;
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				PRINT("Failed")  ;
	PRINT("25 at End\n%!'+b", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER)) ;
	xBufClose(psBuf) ;
}
