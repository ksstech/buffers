idf_component_register(
	SRCS "x_buffers.c" "x_ubuf.c" "x_uubuf.c"
	INCLUDE_DIRS "."
	PRIV_INCLUDE_DIRS
	REQUIRES common
	PRIV_REQUIRES rtos-support printf
)
# common hal_esp32  syslog
