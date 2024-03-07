// x_buffers.c - Copyright (c) 2014-24 Andre M. Maree / KSS Technologies (Pty) Ltd.

#include "hal_config.h"
#include "hal_stdio.h"
#include "hal_nvic.h"
#include "x_buffers.h"
#include "FreeRTOS_Support.h"
#include "printfx.h"
#include "syslog.h"
#include "x_errors_events.h"

#include <string.h>

// ############################### BUILD: debug configuration options ##############################

#define	debugFLAG					(0x0000)

#define	debugSTRUCTURE				(debugFLAG & 0x0001)

#define	debugTIMING					(debugFLAG_GLOBAL & debugFLAG & 0x1000)
#define	debugTRACK					(debugFLAG_GLOBAL & debugFLAG & 0x2000)
#define	debugPARAM					(debugFLAG_GLOBAL & debugFLAG & 0x4000)
#define	debugRESULT					(debugFLAG_GLOBAL & debugFLAG & 0x8000)

// ################################### Private/local variables #####################################

buf_t bufTable[configBUFFERS_MAX_OPEN];

#if defined(ESP_PLATFORM)
	spinlock_t muxBuffers = { 0 };
#else
	portMUX_TYPE muxBuffers = { 0 };
#endif

// ############################## Heap and memory de/allocation related ############################

SemaphoreHandle_t	BufSmlLock , BufMedLock, BufLrgLock;
uint8_t	BufSmall[64], BufMedium[128], BufLarge[256];

/**
 * @brief
 * @param BufSize
 */
void *	pvBufTake(size_t BufSize) {
	if (BufSize <= sizeof(BufSmall)) {
		xRtosSemaphoreTake(&BufSmlLock, portMAX_DELAY);
		return BufSmall;
	} else if (BufSize <= sizeof(BufMedium)) {
		xRtosSemaphoreTake(&BufMedLock, portMAX_DELAY);
		return BufMedium;
	} else if (BufSize <= sizeof(BufLarge)) {
		xRtosSemaphoreTake(&BufLrgLock, portMAX_DELAY);
		return BufLarge;
	}
	return (void *) pdFAIL;
}

/**
 * @brief
 * @param pvBuf
 * @return
 */
int	xBufGive(void * pvBuf) {
	if (pvBuf == BufSmall) return xRtosSemaphoreGive(&BufSmlLock);
	else if (pvBuf == BufMedium) return xRtosSemaphoreGive(&BufMedLock);
	else if (pvBuf == BufLarge) return xRtosSemaphoreGive(&BufLrgLock);
	return (BaseType_t) pdFAIL;
}

// ################################# Private/Local support functions ###############################

/**
 * @brief
 * @param psBuf
 */
void xBufCheck(buf_t * psBuf) {
	myASSERT(halCONFIG_inSRAM(psBuf) && halCONFIG_inSRAM(psBuf->pBeg));
	myASSERT(INRANGE(configBUFFERS_SIZE_MIN, psBuf->xSize, configBUFFERS_SIZE_MAX));
	myASSERT((psBuf->pEnd - psBuf->pBeg) == psBuf->xSize);
	myASSERT(psBuf->xUsed <= psBuf->xSize);
	myASSERT(INRANGE(psBuf->pBeg, psBuf->pRead, psBuf->pEnd));
	myASSERT(INRANGE(psBuf->pBeg, psBuf->pWrite, psBuf->pEnd));
}

/**
 * @brief
 * @param psBuf
 */
static void vBufIsrEntry(buf_t * psBuf) {
	if (halNVIC_CalledFromISR() > 0) {					// if called from an ISR
		FF_SET(psBuf, FF_FROMISR);						// just set the flag
	} else {
	#if	defined(ESP_PLATFORM)
		portENTER_CRITICAL(&muxBuffers);				// else disable interrupts
	#else
		taskENTER_CRITICAL();							// else disable interrupts
	#endif
	}
}

/**
 * @brief
 * @param psBuf
 */
static void vBufIsrExit(buf_t * psBuf) {
	if (FF_STCHK(psBuf, FF_FROMISR)) {					// if called from an ISR
		FF_UNSET(psBuf, FF_FROMISR);					// just clear the flag
	} else {
	#if	defined(ESP_PLATFORM)
		portEXIT_CRITICAL(&muxBuffers);					// else re-enable interrupts
	#else
		taskEXIT_CRITICAL();							// else re-enable interrupts
	#endif
	}
}

/**
 * @brief
 * @return
 */
static buf_t * vBufTakePointer(void) {
	#if	defined(ESP_PLATFORM)
	if (muxBuffers.count == 0 && muxBuffers.owner == 0) spinlock_initialize(&muxBuffers);
	#endif
	for (int i = 0; i < configBUFFERS_MAX_OPEN; ++i) {
		if (bufTable[i].pBeg == 0) return &bufTable[i];
	}
	/* If we ASSERT() here something might be recursing hence eating up all structures.
	 * Common cause is if we use an SL_ or IF_SL_ in the socketsX module,
	 * since this will call syslog() which will want to allocate a buffer,
	 * which will call back here, and so we recurse to a crash...... */
	myASSERT(0);
	return NULL;
}

/**
 * @brief
 * @param psBuf
 * @return
 */
static int vBufGivePointer(buf_t * psBuf) {
	for (int i = 0; i < configBUFFERS_MAX_OPEN; i++) {
		if (psBuf == &bufTable[i]) {
			psBuf->pBeg = 0;							/* Mark as closed/unused */
			return erSUCCESS;
		}
	}
	myASSERT(0);
	return erFAILURE;
}

int	xBufCompact(buf_t * psBuf) {
	if (FF_STCHK(psBuf, FF_CIRCULAR) == 1 || FF_STCHK(psBuf, FF_MODEPACK) == 0) {
		return erFAILURE;
	}
	if (psBuf->pRead > psBuf->pBeg) {					// yes, some empty space at start?
		vBufIsrEntry(psBuf);
		memmove(psBuf->pBeg, psBuf->pRead, psBuf->xUsed);
		psBuf->pRead	= psBuf->pBeg;					// reset read pointer to beginning
		psBuf->pWrite	= psBuf->pBeg + psBuf->xUsed;	// recalc the write pointer
		vBufIsrExit(psBuf);
		memset(psBuf->pWrite, 0, psBuf->xSize - psBuf->xUsed);
	}
	return psBuf->xSize - psBuf->xUsed;
}

// ################################### Public/Global functions #####################################

/**
 * @brief
 * @param psBuf
 * @return
 */
int	xBufReport(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	return P("B=%p  E=%p  R=%p  W=%p  S=%d  U=%d",
		psBuf->pBeg, psBuf->pEnd, psBuf->pRead, psBuf->pWrite, psBuf->xSize, psBuf->xUsed);
}

/**
 * @brief		reset input/output pointers, allow to treat as a normal linear buffer
 * @param pCb	pointer to the buffer control structure
 * @param Used	amount of data in buffer, available to be read
 * @return		None
 */
void vBufReset(buf_t * psBuf, size_t Used) {
	vBufIsrEntry(psBuf);
	psBuf->pRead = psBuf->pBeg;							// Setup READ pointers
	psBuf->pWrite = psBuf->pBeg;						// setup WRITE pointers
	psBuf->xUsed = Used;								// indicate (re)used space, if any
	FF_UNSET(psBuf, FF_UNGETC);							// and no ungetc'd character..
	vBufIsrExit(psBuf);
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
}

/**
 * @brief 		populate the control structure fields
 * @param psBuf	pointer to already allocated buffer structure
 * @param pBuf	pointer to already allocated actual buffer space
 * @param Size	bytes of already allocated space
 * @param flags	minimally implemented
 * @param Used	data in buffer, available to be read
 * @return		erSUCCESS
 */
static int xBufReuse(buf_t * psBuf, char * pBuf, size_t Size, u32_t flags, size_t Used) {
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(psBuf) && pBuf != 0 && Used <= Size)
	vBufIsrEntry(psBuf);
	psBuf->pBeg		= pBuf;
	psBuf->pEnd		= pBuf + Size;						// calculate & save end
	psBuf->xSize	= Size;
// Only some flags to be carried forward...
	FF_SET(psBuf, (flags & (FF_MODER | FF_MODEW | FF_MODERW | FF_MODEA | FF_MODEBIN | FF_CIRCULAR | FF_BUFFALOC)));
	vBufIsrExit(psBuf);
	vBufReset(psBuf, Used);
	return erSUCCESS;
}

/**
 * @brief		allocate memory for the buffer and the control structure populate the control structure fields
 * @param pBuf	pointer to the buffer memory to use, 0 to create new
 * @param Size	buffer size to use or create
 * @param flags	based on flags as defined, minimally implemented
 * @param Used	amount of data in buffer, available to be read
 * @return		pointer to the buffer handle or NULL if failed
 */
buf_t * psBufOpen(void * pBuf, size_t Size, u32_t flags, size_t Used) {
	if ((pBuf == NULL) && (INRANGE(configBUFFERS_SIZE_MIN, Size, configBUFFERS_SIZE_MAX) == false)) {
		myASSERT(0);
		return pvFAILURE;
	}
	IF_myASSERT(debugPARAM, Used <= Size);
	buf_t *	psBuf = vBufTakePointer();					// get a free table entry
	if (psBuf != NULL) {								// unused entry found?
		vBufIsrEntry(psBuf);
		if (pBuf == 0) {
			pBuf 	= pvRtosMalloc(Size);					// allocate memory for buffer
			flags	|= FF_BUFFALOC;					// make sure flag is SET !!
			memset(pBuf, 0, Size);
			Used	= 0;								// cannot have used something in a new buffer
		} else {
			flags	&= ~FF_BUFFALOC;					// make sure flag is CLEAR !!
		}
		vBufIsrExit(psBuf);
		xBufReuse(psBuf, pBuf, Size, flags, Used)	;	// setup
	}
	return psBuf;
}

/**
 * @brief	deallocate the memory for the buffer and the control structure
 * @param	psBuf	pointer to the buffer control structure
 * @return	SUCCESS if deleted with error, FAILURE otherwise
 */
int	xBufClose(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	vBufIsrEntry(psBuf);
	char *	pTmp = psBuf->pBeg;								// save pointer for later use..
	int iRV = vBufGivePointer(psBuf);
	if ((iRV == erSUCCESS) && FF_STCHK(psBuf, FF_BUFFALOC)) {
	#if defined( __GNUC__ )
		psBuf->_flags = 0;
	#elif defined( __TI_ARM__ )
		psBuf->flags = 0;
	#endif
		vRtosFree(pTmp);									// return buffer
	}
	vBufIsrExit(psBuf);
	return iRV;
}

/**
 * @brief		get the number of characters in the buffer
 * @param psBuf	pointer to the buffer control structure
 * @return		Number of characters
 */
size_t xBufAvail(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	return psBuf->xUsed;
}

/**
 * @brief		automatically pack the buffer to free up maximum contiguous space
 * @param psBuf	pointer to the buffer control structure
 * @return		Number of empty slots
 */
size_t xBufSpace(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	if (FF_STCHK(psBuf, FF_MODEPACK))
		xBufCompact(psBuf);
	return psBuf->xSize - psBuf->xUsed;				// compacted space available
}

/**
 * @brief		buffer and control structure MUST have been created prior
 * @param psBuf pointer to buffer control structure
 * @param cChr	the char to be written
 * @return		cChr if the char was written or EOF if discarded/overwritten
 */
int	xBufPutC(int cChr, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	int	iRV;
	if ((cChr == CHR_LF) && (FF_STCHK(psBuf, FF_MODEBIN) == 0)) {
		iRV = xBufPutC(CHR_CR, psBuf);
		if (iRV == EOF) return iRV;
	}
	if (psBuf->xSize > psBuf->xUsed) {
		vBufIsrEntry(psBuf);
		*psBuf->pWrite++ = cChr;							// Firstly store char in buffer
		psBuf->xUsed++;									// & adjust the Used counter
		if (psBuf->pWrite == psBuf->pEnd)					// Last character written in last slot &
			psBuf->pWrite = psBuf->pBeg;					// yes, wrap write pointer to start
		vBufIsrExit(psBuf);
		iRV = cChr;
	} else {
		iRV = EOF;
	}
	return iRV;
}

/**
 * @brief		buffer and control structure MUST have been created prior
 * @param psBuf	pointer to buffer control structure
 * @return		character read else EOF
 */
int	xBufGetC(buf_t * psBuf) {
	if (xBufAvail(psBuf) == 0) return EOF;
	vBufIsrEntry(psBuf);
	int cChr = *psBuf->pRead++;							// read character & adjust pointer
	psBuf->xUsed--;									// & adjust the Used counter
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {					// Circular buffer ...
		if (psBuf->pRead == psBuf->pEnd) {				// and at end of buffer?
			psBuf->pRead = psBuf->pBeg;				// yes, wrap to start
		}
	} else {											// linear buffer ?
		if (psBuf->xUsed == 0) {						// Handle pointer reset if empty
			psBuf->pWrite	= psBuf->pBeg;
			psBuf->pRead	= psBuf->pBeg;
		}
	}
	vBufIsrExit(psBuf);
	return cChr;
}

/**
 * @brief
 * @param psBuf
 * @return
 */
int xBufPeek(buf_t * psBuf) {
	if (xBufAvail(psBuf) == 0) return EOF;
	return *psBuf->pRead;								// read character
}

/**
 * @brief
 * @param pBuf
 * @param Number
 * @param psBuf
 * @return
 */
char * pcBufGetS(char * pBuf, int Number, buf_t * psBuf) {
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(pBuf))
	char *	pTmp = pBuf;
	while (Number > 1) {
		int	cChr = xBufGetC(psBuf);
		if (cChr == EOF) {								// EOF reached?
			*pTmp = CHR_NUL;							// terminate buffer
			return NULL;								// indicate EOF before NEWLINE
		}
		if (cChr == CHR_LF) {							// end of string reached ?
			*pTmp = cChr;								// store the NEWLINE
			*pTmp = CHR_NUL;							// terminate buffer
			return pBuf;								// and return a valid state
		}
		if ((cChr == CHR_CR) && (FF_STCHK(psBuf, FF_MODEBIN) == false)) {
			continue;
		}
		*pTmp++ = cChr;								// store the character, adjust the pointer
		Number--;										// and update remaining chars to read
	}
// If we get here we have read (Number - 1) characters and still no NEWLINE
	*pTmp = CHR_NUL;									// terminate buffer
	return pBuf;										// and return a valid state
}

/**
 * @brief		add 1 or more structures to the packet payload
 * @param pvBuf	pointer to new payload data
 * @param Size	of structure/unit to be added
 * @param Count	of items of the structure/unit to be added
 * @param psBuf	pointer to the buffer structure
 * @return		number of bytes allocated to buffer or an error code
 */
size_t xBufWrite(void * pvBuf, size_t Size, size_t Count, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(pvBuf))
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {
		IF_myASSERT(debugRESULT, 0)
		return 0;										// indicate nothing written
	}

	Count *= Size;										// calculate requested number of BYTES
	if (Count > (psBuf->pEnd - psBuf->pWrite)) {		// write size bigger than available to end?
		xBufCompact(psBuf);							// compact up, if possible
		Count = psBuf->pEnd - psBuf->pWrite;			// then adjust...
	}
	vBufIsrEntry(psBuf);
	memcpy(psBuf->pWrite, pvBuf, Count);				// move contents across
	psBuf->pWrite	+= Count;							// update the payload pointers and length counters
	psBuf->xUsed	+= Count;
	vBufIsrExit(psBuf);
	return Count;
}

/**
 * @brief
 * @param pvBuf
 * @param Size
 * @param xLen
 * @param psBuf
 * @return
 */
size_t xBufRead(void * pvBuf, size_t Size, size_t Count, buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	IF_myASSERT(debugPARAM, halCONFIG_inSRAM(pvBuf))
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {
		IF_myASSERT(debugRESULT, 0)
		return 0;										// indicate nothing read
	}
	if (Size == 0 || Count == 0) {
		IF_myASSERT(debugRESULT, 0)
		return 0;
	}

	Count *= Size;										// calculate requested number of BYTES
	if (Count > psBuf->xUsed) {							// If more requested than available,
		Count = psBuf->xUsed;							// then adjust...
	}
	vBufIsrEntry(psBuf);
	memcpy(pvBuf, psBuf->pRead, Count);				// move contents across
	psBuf->pRead	+= Count;							// update READ pointer for next
	psBuf->xUsed	-= Count;							// adjust remaining count
	if (psBuf->xUsed == 0) {
		psBuf->pRead = psBuf->pWrite = psBuf->pBeg;	// reset all to start
	}
	vBufIsrExit(psBuf);
	return Count;
}

/**
 * @brief
 * @param psBuf
 * @param Offset
 * @param whence
 * @param flags
 * @return
 */
int	xBufSeek(buf_t * psBuf, int Offset, int whence, int flags) {
char * pTmp;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {					// working on CIRCULAR buffer
		IF_myASSERT(debugRESULT, 0);
		return erFAILURE;								// yes, abort
	}
	IF_P(debugSTRUCTURE, "[Seek 1] B=%p R=%p W=%p S=%d U=%d\r\n", psBuf->pBeg, psBuf->pRead, psBuf->pWrite, psBuf->xSize, psBuf->xUsed);

	vBufIsrEntry(psBuf);
	if (flags & FF_MODER) {
		pTmp =  (whence == SEEK_SET)	? psBuf->pBeg + Offset :
				(whence == SEEK_CUR)	? psBuf->pRead + Offset :
				(whence == SEEK_END)	? psBuf->pEnd + Offset : psBuf->pRead;
		if (pTmp < psBuf->pBeg) {
			myASSERT(0);
			pTmp = psBuf->pBeg;
		} else if (pTmp > psBuf->pEnd) {
			myASSERT(0);
			pTmp = psBuf->pEnd;
		}
		psBuf->pRead = pTmp;
	}
	if (flags & FF_MODEW) {
		pTmp =  (whence == SEEK_SET)	? psBuf->pBeg + Offset :
				(whence == SEEK_CUR)	? psBuf->pWrite + Offset :
				(whence == SEEK_END)	? psBuf->pEnd + Offset : psBuf->pWrite;
		if (pTmp < psBuf->pBeg) {			// seek pos BEFORE start of buffer?
			myASSERT(0)	;					// yes !!!
			pTmp = psBuf->pBeg;
		} else if (pTmp > psBuf->pEnd) {	// seek pos BEYOND end of buffer?
			myASSERT(0)	;					// yes,
			pTmp = psBuf->pEnd;
		}
		psBuf->pWrite = pTmp;
	}
	psBuf->xUsed = psBuf->pWrite - psBuf->pRead;
	vBufIsrExit(psBuf);

	IF_myASSERT(debugRESULT, (psBuf->xUsed <= psBuf->xSize));
	IF_P(debugSTRUCTURE, "[Seek 2] B=%p R=%p W=%p S=%d U=%u\r\n", psBuf->pBeg, psBuf->pRead, psBuf->pWrite, psBuf->xSize, psBuf->xUsed);
	return erSUCCESS;
}

/**
 * @brief		return index of read or write pointer into the buffer
 * @param psBuf
 * @param flags
 * @return
 */
int	xBufTell(buf_t * psBuf, int flags) {
	int	iRV = erFAILURE;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {				// working on circular buffer
		IF_myASSERT(debugRESULT, 0)
		return erFAILURE;							// yes, abort..
	}

	// Can only ask for MODER or MODEW not both or MODERW
	if (((flags & FF_MODER) && (flags & FF_MODEW)) || (flags & FF_MODERW)) {
		IF_myASSERT(debugRESULT, 0)
		return erFAILURE;
	}
	vBufIsrEntry(psBuf);
	if (flags & FF_MODER) {
		iRV =  psBuf->pRead - psBuf->pBeg;
	} else if (FF_STCHK(psBuf, FF_MODEW)) {
		iRV = psBuf->pWrite - psBuf->pBeg;
	}
	vBufIsrExit(psBuf);
	return iRV;
}

/**
 * @brief
 * @param psBuf
 * @param flags
 * @return
 */
char * pcBufTellPointer(buf_t * psBuf, int flags) {
char * pcRetVal = (char *) erFAILURE;
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	if (FF_STCHK(psBuf, FF_CIRCULAR)) {				// working on circular buffer
		IF_myASSERT(debugRESULT, 0)
		return pcRetVal;
	}

	// Can only ask for MODER or MODEW not both nor MODERW
	if (((flags & FF_MODER) && (flags & FF_MODEW)) || (flags & FF_MODERW)) {
		IF_myASSERT(debugRESULT, 0)
		return pcRetVal;
	}
	if (flags & FF_MODER) pcRetVal = psBuf->pRead;
	else if (flags & FF_MODEW) pcRetVal = psBuf->pWrite;
	IF_myASSERT(debugRESULT, halCONFIG_inSRAM(pcRetVal))
	return pcRetVal;
}

/**
 * @brief	Treat the buffer contents as a string and do not try to interpret it.
 * 			All embedded modifier and specifier characters will be ignored.
 * 			Any embedded NUL characters will be interpreted as string terminators
 * 			hence possibly causing premature termination of the buffer output
 * @param 	psBuf	pointer to the managed buffer to be printed
 * @return	number of characters printed
 */
int	xBufPrintClose(buf_t * psBuf) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	IF_myASSERT(debugPARAM, psBuf->xUsed > 0);
	int iRV = nprintfx(psBuf->xUsed, "%s", psBuf->pRead);
	xBufClose(psBuf);
	return iRV;
}

/**
 * @brief	output buffer contents to syslog host and close the buffer
 * @param	pointer to the managed buffer to be printed
 * @param	Syslog priority to be used
 * @return	status of the buffer close event
 */
int	xBufSyslogClose(buf_t * psBuf, u32_t Prio) {
	IF_EXEC_1(debugSTRUCTURE, xBufCheck, psBuf);
	IF_myASSERT(debugPARAM, psBuf->xUsed > 0);
	SL_LOG(Prio, "%.*s", psBuf->xUsed, psBuf->pRead);
	return xBufClose(psBuf);
}

#define	bufSIZE		100
#define	bufSTEP		10

void vBufUnitTest(void) {
	int	iRV;
	buf_t * psBuf = psBufOpen(0, bufSIZE, FF_MODER|FF_MODEW, 0);
	for(int a = 0; a < bufSIZE; a += bufSTEP) {
		for(int b = 0; b < bufSTEP; b++) {
			iRV = xBufPutC(b + '0', psBuf);
			if (iRV != (b + '0')) 	P("Failed");
		}
	}
	// buffer should be full
	if (xBufAvail(psBuf) != bufSIZE) 											P("Failed");
	if (xBufSpace(psBuf) != 0) 													P("Failed");
	if (pcBufTellPointer(psBuf, FF_MODER) != psBuf->pBeg) 						P("Failed");
	// because we are using the xUsed value to track space, pWrite should be wrapped around
	if (pcBufTellPointer(psBuf, FF_MODEW) != psBuf->pRead) 						P("Failed");

	// make sure that we get an error if we write another char...
	if (xBufPutC('Z', psBuf) != EOF) 											P("Failed");

	// now read the buffer empty and verify the contents
	for(int a = 0; a < bufSIZE; a++) {
		if (xBufGetC(psBuf) != ('0' + (a % bufSTEP)))							P("Failed");
	}

	// make sure we get an EOF error if we try read another char
	if (xBufGetC(psBuf) != EOF)													P("Failed");

	// at this stage (empty) the pointers should both automatically be reset to the start
	if ((psBuf->pRead != psBuf->pWrite) || (psBuf->pRead != psBuf->pBeg))		P("Failed");

	// seek the write pointer to the end, effectively making all content available again.
	if (xBufSeek(psBuf, bufSIZE, SEEK_SET, FF_MODEW) != erSUCCESS)				P("Failed");
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				P("Failed A=%d - B=%d", xBufAvail(psBuf), xBufSpace(psBuf));

	// rewind the write pointer, make effectively empty
	if (xBufSeek(psBuf, -bufSIZE, SEEK_END, FF_MODEW) != erSUCCESS)				P("Failed");
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				P("Failed");

	// move write pointer to middle
	if (xBufSeek(psBuf, bufSIZE/2, SEEK_SET, FF_MODEW) != erSUCCESS)			P("Failed");
	if (xBufAvail(psBuf) != xBufSpace(psBuf))									P("Failed");

	// move read pointer to middle
	if (xBufSeek(psBuf, bufSIZE/2, SEEK_SET, FF_MODER) != erSUCCESS)			P("Failed");
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				P("Failed");

	// move both read & write pointers simultaneously to start
	if (xBufSeek(psBuf, -bufSIZE/2, SEEK_CUR, FF_MODER|FF_MODEW) != erSUCCESS)	P("Failed");
	if ((psBuf->pBeg != psBuf->pRead) && (psBuf->pBeg != psBuf->pWrite))		P("Failed");
	if ((xBufAvail(psBuf) != 0) || (xBufSpace(psBuf) != bufSIZE))				P("Failed");

	if (xBufSeek(psBuf, bufSIZE, SEEK_SET, FF_MODEW) != erSUCCESS)				P("Failed");
	printfx("Avail=100%%\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));
	xBufSeek(psBuf, bufSIZE / 2, SEEK_SET, FF_MODER);
	printfx("Avail=50%%\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));
	xBufSeek(psBuf, -(bufSIZE / 4), SEEK_CUR, FF_MODER);
	printfx("Avail=75%%\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));

	char cBuffer[50];
	xBufSeek(psBuf, 0, SEEK_SET, FF_MODER);
	xBufSeek(psBuf, 0, SEEK_END, FF_MODEW);
	// read pointer should be at start and write pointer at the end.
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				P("Failed A=%d - B=%d", xBufAvail(psBuf), xBufSpace(psBuf));

	// read first 25 characters at start of buffer, no compacting should have happened
	if (xBufRead(cBuffer, 5, 5, psBuf) != 25)									P("Failed");
	printfx("Avail=75\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));
	printfx("cBuffer=25\r\n%!'+hhY", 25, cBuffer);
	if ((xBufAvail(psBuf) != 75) || (xBufSpace(psBuf) != 25))					P("Failed");

	// try to write 25 chars just read, should fail since FF_MODEPACK not enabled
	if (xBufWrite(cBuffer, 5, 5, psBuf) != 0)									P("Failed");
	if ((xBufAvail(psBuf) != 75) || (xBufSpace(psBuf) != 25))					P("Failed");
	printfx("Avail=75\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));

	FF_SET(psBuf, FF_MODEPACK);
	// try to write 25 chars just read, should be placed at end after compacting...
	if (xBufWrite(cBuffer, 5, 5, psBuf) != 25)									P("Failed");
	if ((xBufAvail(psBuf) != bufSIZE) || (xBufSpace(psBuf) != 0))				P("Failed");
	printfx("25 at End\r\n%!'+hhY", xBufAvail(psBuf), pcBufTellPointer(psBuf, FF_MODER));
	xBufClose(psBuf);
}
