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
			u8_t f_init:1;
			u8_t f_alloc:1;			// buffer malloc'd
			u8_t f_struct:1;		// struct malloc'd
			u8_t f_nolock:1;
			u8_t f_history:1;
			u8_t f_spare:3;
		};
		u8_t f_flags;				// module flags
	};
} ubuf_t;
DUMB_STATIC_ASSERT(sizeof(ubuf_t) == (12 + sizeof(char *) + sizeof(SemaphoreHandle_t)));

// ################################### EXTERNAL FUNCTIONS ##########################################

/**
 * @brief		set default size of new buffers to be created
 * @param[in]	NewSize - new default size to be used
 * @return		new size that has been set, might be different from size specified (if invalid)
 */
size_t xUBufSetDefaultSize(size_t);

/**
 * @brief		get number of bytes used in buffer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		positive integer 0 or greater
 */
int	xUBufGetUsed(ubuf_t * psUB);

/**
 * @brief		get number of free byte slots in buffer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		positive integer 0 or greater
 */
int	xUBufGetSpace(ubuf_t * psUB);

/**
 * @brief		get number of bytes that can be read as a contiguous block
 * @param[in]	psUB - pointer to buffer control structure
 * @return		positive integer 0 or greater
 */
int xUBufGetUsedBlock(ubuf_t * psUB);

/**
 * @brief		empty buffer using the block handler supplied
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]	hdlr - block write handler API
 * @return		0+ value (number of bytes written) else < 0 (error code)
 * @note		Buffer will be emptied in 1 or 2 bursts. depending on state of pointers.
 */
int xUBufEmptyBlock(ubuf_t * psUB, int (*hdlr)(u8_t *, ssize_t));

/**
 * @brief		read a character from the buffer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		character read or erFAILURE/EOF with errno set
 */
int	xUBufGetC(ubuf_t *psUB);

/**
 * @brief		write character to the buffer
 * @param[in]	psUB pointer to buffer control structure
 * @param[in]	iChr characer to be written
 * @return		character written or 0 (if O_NONBLOCK) with EAGAIN set
 */
int	xUBufPutC(ubuf_t * psUB, int iChr);

/**
 * @brief		read character string up to CR or buffer full
 * @param[in]	pBuf - pointer to buffer where string to be stored
 * @param[in]	BufSize - size of buffer supplied
 * @param[in]	psUB - pointer to buffer control structure
 * @return		pointer to buffer supplied, NULL in none read or EOF before line terminator
 */
char * pcUBufGetS(char * pBuf, int BufSize, ubuf_t *psUB);

/**
 * @brief		read multiple characters into the buffer provided
 * @param[in]	psUB - pointer to buffer control structure
 * @return		number of characters read or erFAILURE/EOF with errno set
 */
int	xUBufRead(ubuf_t *psUB, const void * pBuf, size_t Size);

/**
 * @brief		write multiple characters to the buffer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		number of characters written or 0 (if O_NONBLOCK) with EAGAIN set
 */
ssize_t xUBufWrite(ubuf_t * psUB, const void * pBuf, size_t Size);

/**
 * @brief		return the buffer read pointer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		pointer to next character to be read
 */
u8_t * pcUBufTellRead(ubuf_t * psUB);

/**
 * @brief		return the buffer write pointer
 * @param[in]	psUB - pointer to buffer control structure
 * @return		pointer to next character store location in buffer
 */
u8_t * pcUBufTellWrite(ubuf_t * psUB);

/**
 * @brief		step the buffer read pointer specified number of positions
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]	Step - number of bytes to adjust the pointer with
 * @note		effectively steps over/discards number of characters
 */
void vUBufStepRead(ubuf_t * psUB, int Step);

/**
 * @brief		step the buffer read pointer specified number of positions
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]	Step - number of bytes to adjust the pointer with
 * @note		effectively steps over/"random fills" number of locations
 */
void vUBufStepWrite(ubuf_t * psUB, int Step);

/**
 * @brief		Using the supplied uBuf structure, initialises the members as required
 * @param[in]	psUB structure to initialise
 * @param[in]	pcBuf preallocated buffer, if NULL will malloc
 * @param[in]	BufSize size of preallocated buffer, or size to be allocated
 * @param[in]	Used If preallocated buffer, portion already used
 * @return	pointer to the buffer structure
 */
ubuf_t * psUBufCreate(ubuf_t * psUB, u8_t * pcBuf, size_t BufSize, size_t Used);

/**
 * @brief		Delete semaphore and free allocated (buffer and/or structure) memory if allocated
 * @param[in]	psUB structure to destroy
 */
void vUBufDestroy(ubuf_t *psUB);

/**
 * @brief		empty buffer, discard anything previously written but not yet read
 * @param[in]	psUB structure to reset
 */
void vUBufReset(ubuf_t *psUB);

// VFS support functions
void vUBufInit(void);

// HISTORY support functions

/**
 * @brief		copy previous (older) command added to buffer supplied
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]
 * @param[in]
 * @return		number of characters copied
 */
int xUBufStringNxt(ubuf_t * psUB, u8_t * pu8Buf, int Size);

/**
 * @brief		copy next (newer) command to buffer supplied
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]
 * @param[in]
 * @return		number of characters copied
 */
int xUBufStringPrv(ubuf_t * psUB, u8_t * pu8Buf, int Size);

/**
 * @brief		Add characters from buffer supplied to end of buffer
 * @param[in]	psUB - pointer to buffer control structure
 * @param[in]
 * @param[in]
 * 				If insufficient free space, delete complete entries starting with oldest
 */
void vUBufStringAdd(ubuf_t * psUB, u8_t * pu8Buf, int Size);

struct report_t;
int vUBufReport(struct report_t * psR, ubuf_t *);

#ifdef __cplusplus
}
#endif
