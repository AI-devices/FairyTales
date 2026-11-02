#ifndef I2C_PROTOCOL_H_
#define I2C_PROTOCOL_H_

#include <stdint.h>

typedef enum
{
    CMD_GET_STATUS = 1,
    CMD_RECV_MSG,
    CMD_SEND_DATA,
    CMD_START,
    CMD_STOP,
    CMD_PAUSE,
    CMD_RESUME,
} I2CS_CMD;

typedef enum
{
    ST_IDLE = 1,
    ST_BUSY,
    ST_PAUSED,
    ST_OUTPUT_RDY,
} I2CS_STATUS;

typedef enum
{
    ERR_OK = 1,
    ERR_NOT_RDY,
} I2CS_ERROR;

inline const char* cmd_to_str(uint8_t cmd)
{
    switch (cmd) {
    case CMD_GET_STATUS:
        return "CMD_GET_STATUS";
    case CMD_RECV_MSG:
        return "CMD_RECV_MSG";
    case CMD_SEND_DATA:
        return "CMD_SEND_DATA";
    case CMD_STOP:
        return "CMD_STOP";
    case CMD_PAUSE:
        return "CMD_PAUSE";
    case CMD_RESUME:
        return "CMD_RESUME";
    default:
        return "";
    }
}

inline const char* status_to_str(uint8_t status)
{
    switch (status) {
    case ST_IDLE:
        return "ST_IDLE";
    case ST_BUSY:
        return "ST_BUSY";
    case ST_PAUSED:
        return "ST_PAUSED";
    case ST_OUTPUT_RDY:
        return "ST_OUTPUT_RDY";
    default:
        return "";
    }
}

#define PAYLOAD_SIZE          512
#define PAYLOAD_AUDIO_SAMPLES (PAYLOAD_SIZE / sizeof(int16_t))

#pragma pack(push, 1)
typedef struct
{
    union
    {
        uint8_t cmd;
        uint8_t status;
    };
    uint16_t data_length;
} transaction_t;

typedef struct
{
    uint8_t error;
    uint8_t index;
    uint8_t total;
    uint16_t data_length;
    uint16_t crc;
    uint8_t data[PAYLOAD_SIZE];
} data_packet_t;
#pragma pack(pop)

#endif // I2C_PROTOCOL_H_
