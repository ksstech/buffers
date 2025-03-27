// x_uubuf.c - Copyright (c) 2020-24 Andre M. Maree / KSS Technologies (Pty) Ltd.

#include "hal_platform.h"

#include "x_uubuf.h"
#include "FreeRTOS_Support.h"
#include "report.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ################################### Global/public functions #####################################

int	xUUBufPutC(uubuf_t * psUUBuf, int cChr) {
	if (psUUBuf->Used == psUUBuf->Size)
		return EOF;										// full, return error
	*(psUUBuf->pBuf + psUUBuf->Idx)	= cChr;				// store character in buffer, adjust pointer
	++psUUBuf->Idx;
	++psUUBuf->Used;
	return cChr;
}

int	xUUBufGetC(uubuf_t * psUUBuf) {
	if (xUUBufAvail(psUUBuf) == 0)
		return EOF;
	--psUUBuf->Used;									// adjust the Used counter
	return *(psUUBuf->pBuf + psUUBuf->Idx++);			// read character & adjust pointer
}

char * pcUUBufGetS(char * pBuf, int Number, uubuf_t * psUUBuf) {
	char *	pTmp = pBuf;
	while (Number > 1) {
		int cChr = xUUBufGetC(psUUBuf);
		if (cChr == EOF) {								// EOF reached?
			*pTmp = 0;									// terminate buffer
			return NULL;								// indicate EOF before NEWLINE
		}
		if (cChr == CHR_LF) {							// end of string reached ?
			*pTmp = cChr;								// store the NEWLINE
			*pTmp = 0;									// terminate buffer
			return pBuf;								// and return a valid state
		}
		if (cChr == CHR_CR) continue;
		*pTmp++ = cChr;								// store the character, adjust the pointer
		Number--;										// and update remaining chars to read
	}
	// If we get here we have read (Number - 1) characters and still no NEWLINE
	*pTmp = 0;											// terminate buffer
	return pBuf;										// and return a valid state
}

int	xUUBufCreate(uubuf_t * psUUBuf, char * pcBuf, size_t BufSize, size_t Used) {
	psUUBuf->Size = BufSize;
	psUUBuf->Idx = 0;
	if (pcBuf) {
		psUUBuf->pBuf = pcBuf;
		psUUBuf->Used = Used;
		psUUBuf->Alloc = 0;								// show memory as provided, NOT allocated
	} else {
		psUUBuf->pBuf = malloc(psUUBuf->Size);
		psUUBuf->Used = 0;
		psUUBuf->Alloc = psUUBuf->Size;					// show memory as ALLOCATED
	}
	if (psUUBuf->Used == 0)
		memset(psUUBuf->pBuf, 0, psUUBuf->Size);		// clear buffer ONLY if nothing to be used
	return psUUBuf->Size;
}

void vUUBufDestroy(uubuf_t * psUUBuf) {
	if (psUUBuf->Alloc)
		free(psUUBuf->pBuf);
}

void vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj) {
	if (Adj < 0) {
		psUUBuf->Idx -= Adj;
	} else {
		psUUBuf->Idx += Adj;
	}
	psUUBuf->Used += Adj;
}

int vUUBufReport(report_t * psR, uubuf_t * psUUBuf) {
	return PX("P=%p  B=%p  I=%d  S=%d  U=%d  A=%d\r\n%!'+hhY%s", psUUBuf, psUUBuf->pBuf, psUUBuf->Idx, 
			psUUBuf->Size, psUUBuf->Used, psUUBuf->Alloc, psUUBuf->Used, psUUBuf->pBuf, fmTST(aNL) ? strNLx2 : strNL);
}
