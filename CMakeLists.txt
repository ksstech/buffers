idf_component_register( SRCS x_buffers.c x_ubuf.c x_uubuf.c 
						INCLUDE_DIRS . 
						REQUIRES common 
						PRIV_REQUIRES rtos-support buffers printf syslog common hal_esp32
						)
