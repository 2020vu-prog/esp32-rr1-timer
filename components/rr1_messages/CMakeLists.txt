idf_component_register(SRCS "timer.pb-c.c" "aws_bandaid.c" "timestamp.pb-c.c"
                       REQUIRES protobuf-c  mbedtls
		       INCLUDE_DIRS "include")
