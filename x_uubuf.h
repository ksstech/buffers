/*
 * x_uubuf.h
 */

#pragma	once

#include <stdint.h>
#include <sys/types.h>

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################


// ###################################### BUILD : CONFIG definitions ###############################

#define	pbufSIZE_MINIMUM			128
#define	pbufSIZE_DEFAULT			1024
#define	pbufSIZE_MAXIMUM			32768

// ####################################### enumerations ############################################


// ####################################### structures  #############################################

typedef	struct __attribute__((packed)) uubuf_t {
	char * pBuf;
	u16_t Idx;
	u16_t Size;
	u16_t Used;
	u16_t Alloc;
} uubuf_t;

// ################################### EXTERNAL FUNCTIONS ##########################################

inline size_t xUUBufSpace(uubuf_t * psUUBuf) { return psUUBuf->Size - psUUBuf->Used; }
inline size_t xUUBufAvail(uubuf_t * psUUBuf) { return psUUBuf->Used; }
inline char * pcUUBufPos(uubuf_t * psUUBuf) { return psUUBuf->pBuf + psUUBuf->Idx; }

int	xUUBufPutC(uubuf_t * psUUBuf, int cChr);
int	xUUBufGetC(uubuf_t * psUUBuf);
char * pcUUBufGetS(char * pBuf, int Number, uubuf_t * psUUBuf);
int	xUUBufCreate(uubuf_t * psUUBuf, char * pBuf, size_t BufSize, size_t Used);
void vUUBufDestroy(uubuf_t * psUUBuf);
void vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj);
void vUUBufReport(uubuf_t * psUUBuf);

#ifdef __cplusplus
}
#endif
