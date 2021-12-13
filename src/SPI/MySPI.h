#ifndef MYSPI_H
#define MYSPI_H

#include <stdint.h>
#include <string.h>
#include <ESP32SPISlave.h> 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern ESP32SPISlave slave;

extern const uint32_t BUFFER_SIZE;
extern uint8_t spi_slave_tx_buf[];
extern uint8_t spi_slave_rx_buf[];

// the core number the task should run on (0 or 1)
extern const uint8_t CORE_TASK_SPI_SLAVE;
extern const uint8_t CORE_TASK_PROCESS_BUFFER;

extern uint32_t transactionNo;
extern volatile bool cs_last;
extern volatile bool cs;

extern uint8_t rk1Stell[38];
extern int rkIdx;

extern uint8_t requestDataFromMaster_stage;
extern bool requestDataFromMaster;
extern uint32_t pollDataDelay_ms;
extern int64_t lastDataRequestTime;

extern char* registerNames[];
extern uint16_t registerValues[];

void set_tx_buffer(uint8_t value);
void set_tx_buffer(uint8_t* command, uint32_t size);
void setupSPISlave ();
uint16_t MODBUS_CRC16_v3(const unsigned char *buf, unsigned int len);

#endif