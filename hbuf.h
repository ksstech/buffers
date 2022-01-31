/*
 * hbuf.h - Copyright 2022 Andre M. Maree/KSS Technologies (Pty) Ltd.
 */

#pragma	once

#include	<stdint.h>
#include	<sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################

#define cliSIZE_HBUF	1024

// ###################################### BUILD : CONFIG definitions ###############################


// ####################################### enumerations ############################################


// ####################################### structures  #############################################

typedef struct __attribute__((packed)) {
	uint16_t iNo1;					// index to start of 1st saved command
	uint16_t iCur;					// index to start of selected command in buffer
	uint16_t iFree;					// index to 1st free location in buffer
	uint16_t Count;					// number of commands in buffer
	uint8_t Buf[cliSIZE_HBUF];
} hbuf_t;

// ################################### EXTERNAL FUNCTIONS ##########################################

void vHBufInit(hbuf_t * psHB, uint8_t * pu8Buf, size_t Size);
int xHBufAvail(hbuf_t * psHB);
void vHBufFree(hbuf_t * psHB, size_t Size);
void vHBufAddCmd(hbuf_t * psHB, uint8_t * pu8Buf, size_t Size);

int vHBufNxtCmd(hbuf_t * psHB, uint8_t * pu8Buf, size_t Size);
int vHBufPrvCmd(hbuf_t * psHB, uint8_t * pu8Buf, size_t Size);

void vHBufReport(hbuf_t * psHB);

#ifdef __cplusplus
}
#endif
