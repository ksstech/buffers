# BUFFERS
set( srcs "x_buffers.c" "x_ubuf.c" "x_uubuf.c" "hbuf.c")
set( include_dirs "." )
set( requires common rtos-support )
set( priv_requires hal_esp32 printfx syslog systiming vfs )
idf_component_register(SRCS ${srcs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires} PRIV_REQUIRES ${priv_requires})
