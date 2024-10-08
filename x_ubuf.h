// x_ubuf.h

#pragma	once

#include "definitions.h"
#include "FreeRTOS_Support.h"

#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

// ##################################### MACRO definitions #########################################


// ####################################### enumerations ############################################

enum { ioctlUBUF_UNDEFINED, ioctlUBUF_I_PTR_CNTL, ioctlUBUF_NUMBER };

// ####################################### structures  #############################################

typedef	struct ubuf_t {
	u8_t * pBuf;
	SemaphoreHandle_t mux;
	volatile u16_t IdxWR;			// index to next space to WRITE to
	volatile u16_t IdxRD;			// index to next char to be READ from
	volatile u16_t Used;
	u16_t Size;
	u16_t _flags;					// stdlib related flags
	u8_t count;						// history command counter
	union {
		struct  __attribute__((packed)) {
			u8_t	f_init:1;
			u8_t	f_alloc:1;		// buffer malloc'd
			u8_t	f_struct:1;		// struct malloc'd
			u8_t	f_nolock:1;
			u8_t	f_history:1;
			u8_t	f_spare:3;
		};
		u8_t	f_flags;				// module flags
	};
} ubuf_t;
DUMB_STATIC_ASSERT(sizeof(ubuf_t) == (12 + sizeof(char *) + sizeof(SemaphoreHandle_t)));

extern ubuf_t sUBuf[];

// ################################### EXTERNAL FUNCTIONS ##########################################

int	xUBufGetUsed(ubuf_t * psUBuf);
int xUBufGetUsedBlock(ubuf_t * psUBuf);
int	xUBufGetSpace(ubuf_t * psUBuf);
int xUBufEmptyBlock(ubuf_t * psUBuf, int (*hdlr)(u8_t *, ssize_t));

u8_t * pcUBufTellWrite(ubuf_t * psUBuf);
u8_t * pcUBufTellRead(ubuf_t * psUBuf);

void vUBufStepWrite(ubuf_t * psUBuf, int Step);
void vUBufStepRead(ubuf_t * psUBuf, int Step);

size_t xUBufSetDefaultSize(size_t);
ubuf_t * psUBufCreate(ubuf_t * psUBuf, u8_t * pcBuf, size_t BufSize, size_t Used);
/**
 * @brief	Delete semaphore and free allocated (buffer and/or structure) memory if allocated
 * @param	psUB structure to destroy
 */
void vUBufDestroy(ubuf_t *);
void vUBufReset(ubuf_t *);
int	xUBufGetC(ubuf_t *);
int	xUBufPutC(ubuf_t *, int);
char * pcUBufGetS(char *, int, ubuf_t *);

// VFS support functions
void vUBufInit(void);

// HISTORY support functions
int xUBufStringNxt(ubuf_t * psUB, u8_t * pu8Buf, int Size);
int xUBufStringPrv(ubuf_t * psUB, u8_t * pu8Buf, int Size);
void vUBufStringAdd(ubuf_t * psUB, u8_t * pu8Buf, int Size);

struct report_t;
int vUBufReport(struct report_t * psR, ubuf_t *);

#ifdef __cplusplus
}
#endif
