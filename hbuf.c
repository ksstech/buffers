/*
 * hbuf.c - Copyright 2022 Andre M. Maree/KSS Technologies (Pty) Ltd.
 */

#include <string.h>

#include "hbuf.h"
#include "hal_variables.h"
#include "FreeRTOS_Support.h"
#include "printfx.h"

// ################################### Global/public functions #####################################

/**
 * @brief
 */
static int xHBufAvail(hbuf_t * psHB) {
	return (psHB->iFree<psHB->iCur) ? (psHB->iCur-psHB->iFree) : (cliSIZE_HBUF-psHB->iFree+psHB->iCur);
}

/**
 * @brief
 */
static void vHBufFree(hbuf_t * psHB, size_t Size) {
	while (xHBufAvail(psHB) < Size) {
		while (psHB->Buf[psHB->iNo1]) {
			++psHB->iNo1;
			psHB->iNo1 %= cliSIZE_HBUF;
		}
		++psHB->iNo1;									// skip over terminating '0'
		--psHB->Count;
		if (psHB->iCur < psHB->iNo1)
			psHB->iCur = psHB->iNo1;
	}
}

/**
 * @brief	Add characters from buffer supplied to end of buffer
 * 			If insufficient free space, delete complete entries starting with oldest
 */
void vHBufAddCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size) {
	if (xHBufAvail(psHB) < Size) {		// insufficient space for current command?
		vHBufFree(psHB, Size);			// drop oldest command[s]
	}
	psHB->iCur = psHB->iFree;			// Save position (of new command 2B added) as current
	for(int i = 0; i <= Size; ++i) {				// include terminating '0' in copy...
		psHB->Buf[psHB->iFree++] = pu8Buf[i];
		psHB->iFree %= cliSIZE_HBUF;
	}
	++psHB->Count;
}

/**
 * @brief	Copy the selected entry from history to buffer supplied
 * @return	number of characters copied
 */
static int vHBufCopyCmd(hbuf_t * psHB, int iStart, u8_t * pu8Buf, size_t Size) {
	int iNow = 0;
	while(iNow < Size) {
		u8_t U8 = psHB->Buf[iStart + iNow];
		if (U8 == 0)
			break;
		pu8Buf[iNow++] = U8;
	}
	return iNow;
}

/**
 * @brief
 */
void vHBufReport(hbuf_t * psHB) {
	if (psHB->Count == 0) {
		printfx("CLI buffer empty\r\n");
		return;
	}
	printfx_lock();
	printfx_nolock("# HBuf #: No1=%d  Cur=%d  Free=%d  Cnt=%d", psHB->iNo1, psHB->iCur, psHB->iFree, psHB->Count);
	u8_t * pNow = &psHB->Buf[psHB->iNo1];
	u8_t u8Len;
	while (true) {
		u8Len = 0;
		while (*pNow) {
			if (u8Len == 0)
				printfx_nolock(" '");
			printfx_nolock("%c", *pNow);
			++pNow;
			if (pNow == &psHB->Buf[cliSIZE_HBUF]) {
				pNow = psHB->Buf;
			}
			++u8Len;
		}
		if (u8Len > 0)
			printfx_nolock("'");
		++pNow;											// step over terminating '0'
		if (pNow == &psHB->Buf[psHB->iFree]) {
			break;
		}
	}
	printfx_nolock("\r\n\n");
	printfx_unlock();
}

// ########################### Commands to support looping through history #########################

/**
 * @brief	copy previous (older) command added to buffer supplied
 * @return	number of characters copied
 */
int vHBufPrvCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size) {
	if (psHB->Count == 1) {
		return vHBufCopyCmd(psHB, psHB->iNo1, pu8Buf, Size);
	}
	int iNow = (psHB->iCur == 0) ? (psHB->iFree - 1) : (psHB->iCur - 1);
	do {
		--iNow;
		if (iNow >= 0) {
			if (psHB->Buf[iNow] == 0) {
				psHB->iCur = ++iNow % cliSIZE_HBUF;
				return vHBufCopyCmd(psHB, psHB->iCur, pu8Buf, Size);
			} else {
				// not yet at terminating '0' NOR start of buffer, hence continue
			}
		} else {
			iNow = cliSIZE_HBUF;							// continue at the end...
		}
	} while(1);
}

/**
 * @brief	copy next (newer) command to buffer supplied
 * @return	number of characters copied
 */
int vHBufNxtCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size) {
	if (psHB->Count == 1) {
		return vHBufCopyCmd(psHB, psHB->iNo1, pu8Buf, Size);
	}
	int iNow = (psHB->iCur == psHB->iFree) ? psHB->iNo1 : psHB->iCur;
	do {
		if (psHB->Buf[iNow] == 0) {
			psHB->iCur = ++iNow % cliSIZE_HBUF;
			return vHBufCopyCmd(psHB, psHB->iCur, pu8Buf, Size);
		}
		++iNow;
		iNow %= cliSIZE_HBUF;
	} while(1);
}
