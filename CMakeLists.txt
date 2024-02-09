# BUFFERS

set( srcs "x_buffers.c" "x_ubuf.c" "x_uubuf.c" "hbuf.c")
set( include_dirs "." )
set( priv_include_dirs )
set( requires "rtos-support vfs" )
set( priv_requires "common printfx syslog systiming" )

idf_component_register(
	SRCS ${srcs}
	INCLUDE_DIRS ${include_dirs}
	PRIV_INCLUDE_DIRS ${priv_include_dirs}
	REQUIRES ${requires}
	PRIV_REQUIRES ${priv_requires}
)
