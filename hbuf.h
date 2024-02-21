// hbuf.h

#pragma	once

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################

#define cliSIZE_HBUF	1024

// ###################################### BUILD : CONFIG definitions ###############################
// ####################################### enumerations ############################################
// ####################################### structures  #############################################

typedef struct __attribute__((packed)) {
	u16_t iNo1;						// index to start of 1st saved command
	u16_t iCur;						// index to start of selected command in buffer
	u16_t iFree;					// index to 1st free location in buffer
	u16_t Count;					// number of commands in buffer
	u8_t Buf[cliSIZE_HBUF];
} hbuf_t;

// ################################### EXTERNAL FUNCTIONS ##########################################

void vHBufAddCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size);
void vHBufReport(hbuf_t * psHB);

int vHBufNxtCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size);
int vHBufPrvCmd(hbuf_t * psHB, u8_t * pu8Buf, size_t Size);

#ifdef __cplusplus
}
#endif
