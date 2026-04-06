#include "gd32e230c_eval.h"
#include "gd32e23x.h"
#include "gd32e23x_it.h"
#include "systick.h"

/* 业务逻辑函数声明 */
void process_adc_logic(void);

int main(void)
{
    systick_config();
    
    /* 定义需要初始化的 ADC 通道列表 (0-7路) */
    uint8_t channels[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    
    /* 定义需要初始化的 OK 信号输出列表 */
    voltage_ok_typedef_enum ok_list[5] = 
    {
        VOLTAGE_OK_2V,
        VOLTAGE_OK_5V,
        VOLTAGE_OK_9V,
        VOLTAGE_OK_36V,
        VOLTAGE_OK_13V,
    };

    /* 1. 初始化基础外设 (LED) */
    gd_eval_led_init(LED1);
    gd_eval_led_init(LED2);
    
    /* 2. 初始化 OK 信号输出引脚 */
    gd_eval_voltage_ok_init(ok_list, 5);
    
    /* 3. 初始化 ADC 通道映射并开启 DMA 自动化搬运 */
    gd_eval_adc_init_multi_channel(channels, 8);

    while (1) 
    {
        /* 等待 DMA 搬完一轮数据 (由 DMA_Channel0_IRQHandler 中断置位) */
        if (adc_data_ready_flag) 
        {
            adc_data_ready_flag = 0;
            process_adc_logic(); 
        }
        
        /* 进入低功耗睡眠状态，由下一次 DMA 中断唤醒 CPU */
        __WFI();
    }
}

/**
 * @brief 核心业务逻辑：根据 8 路采样值决定 5 路 OK 信号的状态
 * @note  阈值 2000 仅为示例，请根据实际分压电路计算 (转换公式: 电压 = 采样值/4095 * 3.3V * 分压倍数)
 */
void process_adc_logic(void)
{
    /* 判定 2V 通道 */
    if (gd_eval_adc_get_value(ADC_CH_2V) > 2000) 
    { 
        gd_eval_voltage_ok_on(VOLTAGE_OK_2V);
    } 
    else 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_2V);
    }

    /* 判定 5V 通道 */
    if (gd_eval_adc_get_value(ADC_CH_5V) > 2000) 
    { 
        gd_eval_voltage_ok_on(VOLTAGE_OK_5V);
    } 
    else 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_5V);
    }

    /* 判定 9V 通道 */
    if (gd_eval_adc_get_value(ADC_CH_9V) > 2000) 
    { 
        gd_eval_voltage_ok_on(VOLTAGE_OK_9V);
    } 
    else 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_9V);
    }

    /* 判定 36V 通道 */
    if (gd_eval_adc_get_value(ADC_CH_36V) > 2000) 
    { 
        gd_eval_voltage_ok_on(VOLTAGE_OK_36V);
    } 
    else 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_36V);
    }

    /* 判定 13V 通道 */
    if (gd_eval_adc_get_value(ADC_CH_13V) > 2000) 
    { 
        gd_eval_voltage_ok_on(VOLTAGE_OK_13V);
    } 
    else 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_13V);
    }
}

