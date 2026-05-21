#pragma once

#include <stddef.h>

/**
 * @brief Initialize the interface connected to the GPS (I2C or UART)
 */
void nmea_rr1_init_interface(void);

/**
 * @brief Get one NMEA message line from the GPS.
 *
 * Note, the returned buffer pointer is only valid until the next call to this
 * function. If no data was received, out_line_len is zero.
 *
 * @param[out] out_line_buf  output, pointer to the buffer containing the NMEA
 * message
 * @param[out[ out_line_len  length of the message, in bytes
 * @param[in] timeout_ms  timeout for reading the message, in milliseconds
 */
void nmea_rr1_read_line(char **out_line_buf, size_t *out_line_len,
                        int timeout_ms);

void nmea_main(void *pvParameter);

// RR1
#define CONFIG_EXAMPLE_UART_NUM 1
#define CONFIG_EXAMPLE_UART_RX 20