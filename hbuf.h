// hbuf.h

#pragma	once

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################

#define cliSIZE_HBUF	1024

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

void vHBufAddCmd(hbuf_t *, u8_t *, size_t);
int vHBufNxtCmd(hbuf_t *, u8_t *, size_t);
int vHBufPrvCmd(hbuf_t *, u8_t *, size_t);

struct report_t;
int xHBufReport(struct report_t *, hbuf_t *);

#ifdef __cplusplus
}
#endif
