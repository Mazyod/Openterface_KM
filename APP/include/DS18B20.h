#ifndef __DS18B20_H
#define __DS18B20_H

#include <stdint.h>

#define DS18B20_OK           0
#define DS18B20_ERROR_BUS    1
#define DS18B20_ERROR_VALUE  2

void DS18B20_Init(void);
uint8_t DS18B20_StartConversion(void);
uint8_t DS18B20_ReadRaw(int16_t *rawValue);
uint8_t DS18B20_ReadTemperature(float *tempC);
int16_t DS18B20_TempToHex(float tempC);
void DS18B20_PrintHex(float tempC);
void DS18B20_TempToBytes(float tempC, uint8_t *out);
void DS18B20_PrintHex(float tempC);
void DS18B20_TempToBytes(float tempC, uint8_t *out);
void DS18B20_Command(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len);
#endif /* __DS18B20_H */
