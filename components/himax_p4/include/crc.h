#ifndef CRC_H_
#define CRC_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t polynomial;
    uint16_t initial_value;
    uint16_t final_xor_value;
    int input_reflected;
    int output_reflected;
} crc16_config_t;

typedef uint16_t crc16_table[256];

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void crc16_init_table(crc16_table table, const crc16_config_t *config);
uint16_t crc16_compute(const crc16_table table, const crc16_config_t *config, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CRC_H_
