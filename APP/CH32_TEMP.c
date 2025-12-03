#include "debug.h"
#include "ch32v20x_adc.h"
#include "DS18B20.h"
#include "Version_selection.h"
s16 Calibrattion_Val = 0;
void ADC_Function_Init(void)
{
    ADC_InitTypeDef  ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    ADC_BufferCmd(ADC1, DISABLE); //disable buffer
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    Calibrattion_Val = Get_CalibrationValue(ADC1);

    ADC_BufferCmd(ADC1, ENABLE); //enable buffer

    ADC_TempSensorVrefintCmd(ENABLE);
}

u16 Get_ADC_Val(u8 ch)
{
    u16 val;

    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    val = ADC_GetConversionValue(ADC1);

    return val;
}

u16 Get_ADC_Average(u8 ch, u8 times)
{
    u32 temp_val = 0;
    u8  t;
    u16 val;

    for(t = 0; t < times; t++) {
        temp_val += Get_ADC_Val(ch);
        Delay_Ms(5);
    }

    val = temp_val / times;

    return val;
}

u16 Get_ConversionVal(s16 val)
{
    if((val + Calibrattion_Val) < 0|| val==0)
        return 0;
    if((Calibrattion_Val + val) > 4095|| val==4095)
        return 4095;
    return (val + Calibrattion_Val);
}
uint8_t Temp_Command(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len)
{
    int16_t raw;
    uint8_t data[3];
    u16 ADC_val;
    s32 val_mv;
    if(version == 1)
    {if (DS18B20_StartConversion() != DS18B20_OK)
     {
         data[0] = STATUS_ERR_TIMEOUT;
         CH9329_SendResponse(addr, cmd_code, data, 1,0);
        return 0;
     }
    //  Wait for conversion (12-bit resolution typically ~750ms)  
     Delay_Ms(750);

     if (DS18B20_ReadRaw(&raw) != DS18B20_OK)
     {
         data[0] = STATUS_ERR_TIMEOUT;
         CH9329_SendResponse(addr, cmd_code, data, 1,0);
         return 0;
     }
    }
    ADC_val = Get_ADC_Average(ADC_Channel_TempSensor, 10);
    Delay_Ms(500);
    ADC_val = Get_ConversionVal(ADC_val);
    val_mv = (ADC_val * 3300 / 4096);
    float temperature_c = raw * 0.0625f;
    int16_t temperature = (int16_t)temperature_c; 

    uint16_t temp_hex = TempSensor_Volt_To_Temper_Hex(val_mv);

    data[0] = STATUS_SUCCESS;
    data[1] = (uint8_t)(temperature & 0xFF);
    data[2] = (uint8_t)(temp_hex & 0xFF);

    CH9329_SendResponse(addr, cmd_code, data, 3,1);
    return 0;
}


