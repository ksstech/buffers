/*
 * x_uubuf.h
 */

#pragma	once

#include	<stdint.h>
#include	<sys/types.h>

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

typedef	struct uubuf_s {
	char *				pBuf ;
	uint16_t			Idx ;
	uint16_t			Size ;
	uint16_t			Used ;
	uint16_t			Alloc ;
} uubuf_t ;

// ################################### EXTERNAL FUNCTIONS ##########################################

inline	size_t	xUUBufSpace(uubuf_t * psUUBuf)			{ return psUUBuf->Size - psUUBuf->Used ; }
inline	size_t	xUUBufAvail(uubuf_t * psUUBuf)			{ return psUUBuf->Used ; }
inline	char *	pcUUBufPos(uubuf_t * psUUBuf)			{ return psUUBuf->pBuf + psUUBuf->Idx ; }

int32_t	xUUBufPutC(uubuf_t * psUUBuf, int32_t cChr) ;
int32_t	xUUBufGetC(uubuf_t * psUUBuf) ;
char *	pcUUBufGetS(char * pBuf, int32_t Number, uubuf_t * psUUBuf) ;
int32_t	xUUBufCreate(uubuf_t * psUUBuf, char * pBuf, size_t BufSize, size_t Used) ;
void	vUUBufDestroy(uubuf_t * psUUBuf) ;
void	vUUBufAdjust(uubuf_t * psUUBuf, ssize_t Adj) ;
void	vUUBufReport(int32_t Handle, uubuf_t * psUUBuf) ;

#ifdef __cplusplus
}
#endif
