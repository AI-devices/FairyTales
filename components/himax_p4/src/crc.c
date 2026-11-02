#include "crc.h"

static uint8_t reflect8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static uint16_t reflect16(uint16_t w)
{
    uint16_t r = 0;
    for (int i = 0; i < 16; ++i) {
        r = (r << 1) | (w & 1);
        w >>= 1;
    }
    return r;
}

void crc16_init_table(crc16_table table, const crc16_config_t* config)
{
    for (int i = 0; i < 256; ++i) {
        uint16_t crc  = 0;
        uint16_t byte = (uint8_t)i;

        if (config->input_reflected) {
            byte = reflect8(byte);
        }

        crc = (config->input_reflected) ? (uint16_t)byte : ((uint16_t)byte << 8);

        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x8000) != 0) {
                crc = (crc << 1) ^ config->polynomial;
            } else {
                crc <<= 1;
            }
        }

        if (config->output_reflected) {
            crc = reflect16(crc);
        }

        table[i] = crc ^ config->final_xor_value;
    }
}

uint16_t crc16_compute(const crc16_table table, const crc16_config_t* config, const uint8_t* data, size_t length)
{
    uint16_t crc = config->initial_value;

    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];

        if (config->input_reflected) {
            // Reflect 8-bit
            uint8_t reflected_byte = 0;
            for (int j = 0; j < 8; ++j) {
                reflected_byte <<= 1;
                reflected_byte |= (byte & 1);
                byte >>= 1;
            }
            byte = reflected_byte;
        }

        uint8_t tbl_index = ((crc >> 8) ^ byte) & 0xFF;
        crc               = (crc << 8) ^ table[tbl_index];
    }

    return crc ^ config->final_xor_value;
}
