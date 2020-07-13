set( srcs "x_buffers.c" "x_ubuf.c" "x_uubuf.c" )
set( include_dirs "." )
set( priv_include_dirs "." )
set( requires common )
set (priv_requires rtos-support buffers printf syslog common hal_esp32 )
idf_component_register(	SRCS ${srcs} 
						INCLUDE_DIRS ${include_dirs}
						REQUIRES ${requires}
						PRIV_REQUIRES ${priv_requires}
						)
