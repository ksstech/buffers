/*
 * x_uubuf.c
 */

#include	"x_uubuf.h"
#include	"x_debug.h"
#include	"x_printf.h"
#include	"x_errors_events.h"

#include	<string.h>

#undef	debugFLAG
#define	debugFLAG					0x0001
#define	debugPARAM					(debugFLAG & 0x0001)

// ################################### Global/public functions #####################################

int32_t	xUUBufPutC(uubuf_t * psUUBuf, int32_t cChr) {
	if (psUUBuf->Used == psUUBuf->Size) {				// if full,
		return EOF ;									// return an error
	}
	*(psUUBuf->pBuf + psUUBuf->Idx)	= cChr ;			// store character in buffer, adjust pointer
	++psUUBuf->Idx ;
	++psUUBuf->Used ;
	return cChr ;
}

int32_t	xUUBufGetC(uubuf_t * psUUBuf) {
	if (xUUBufAvail(psUUBuf) == 0) {						// if nothing there
		return EOF ;									// return an error
	}
	--psUUBuf->Used ;									// adjust the Used counter
	return *(psUUBuf->pBuf + psUUBuf->Idx++) ;			// read character & adjust pointer
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
		psUUBuf->pBuf	= malloc(psUUBuf->Size) ;
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
	if (psUUBuf->Alloc) {
		free(psUUBuf->pBuf) ;
	}
}

void	vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj)	{
	if (Adj < 0) { psUUBuf->Idx -= Adj ; } else { psUUBuf->Idx += Adj ; }
	psUUBuf->Used += Adj;
}

void	vUUBufReport(uubuf_t * psUUBuf) {
	printfx("P=%p  B=%p  I=%d  S=%d  U=%d  A=%d\n",
			psUUBuf, psUUBuf->pBuf, psUUBuf->Idx, psUUBuf->Size, psUUBuf->Used, psUUBuf->Alloc) ;
	if (psUUBuf->Used) {
		printfx("%!'+b", psUUBuf->Used, psUUBuf->pBuf) ;
	}
}

// ################################## Diagnostic and testing functions #############################