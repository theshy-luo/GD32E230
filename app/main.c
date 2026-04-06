#include "gd32e230c_eval.h"
#include "gd32e23x.h"
#include "gd32e23x_it.h"
#include "systick.h"

static uint8_t voltage_nged_flag = 0;

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

    /* 预留 500ms 调试窗口，防止 SWD 引脚被锁定后无法下载程序 */
    delay_1ms(500);

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
    // gd_eval_voltage_ok_on(VOLTAGE_OK_2V); // 2V永远OK
    if (voltage_nged_flag) 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_2V);
        return;
    }

    if (gd_eval_adc_get_value(ADC_CH_2V) >= 2000 && gd_eval_adc_get_value(ADC_CH_2V) <= 2100 && 
        gd_eval_adc_get_value(ADC_CH_V1P4) >= 2000 && gd_eval_adc_get_value(ADC_CH_V1P4) <= 2100) 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_2V);
    }
    else
    {
        goto fail_led2;
    }

    if (gd_eval_adc_get_value(ADC_CH_5V) >= 2000 && 
        gd_eval_adc_get_value(ADC_CH_5V) <= 2100) 
    {
        gd_eval_voltage_ok_on(VOLTAGE_OK_5V);
    }
    else
    {
        goto fail_led2;
    }

    if (gd_eval_adc_get_value(ADC_CH_36V) >= 2000 && gd_eval_adc_get_value(ADC_CH_36V) <= 2100 && 
        gd_eval_adc_get_value(ADC_CH_48V) >= 2000 && gd_eval_adc_get_value(ADC_CH_48V) <= 2100) 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_36V);
    }
    else
    {
        goto fail_led1;
    }

    if (gd_eval_adc_get_value(ADC_CH_9V) >= 2000 && 
        gd_eval_adc_get_value(ADC_CH_9V) <= 2100) 
    {
        gd_eval_voltage_ok_off(VOLTAGE_OK_9V);
    }
    else
    {
        goto fail_led1;
    }

    if (gd_eval_adc_get_value(ADC_CH_13V) >= 2000 && 
        gd_eval_adc_get_value(ADC_CH_13V) <= 2100) 
    {
        gd_eval_voltage_ok_on(VOLTAGE_OK_13V);
    }
    else
    {
        goto fail_led1;
    }

    return;

fail_led1:
    gd_eval_led_on(LED1);
    goto fail_process;
fail_led2:
    gd_eval_led_on(LED2);
fail_process:
    // gd_eval_voltage_ok_on(VOLTAGE_OK_2V);
    gd_eval_voltage_ok_on(VOLTAGE_OK_36V);
    gd_eval_voltage_ok_on(VOLTAGE_OK_9V);
    gd_eval_voltage_ok_off(VOLTAGE_OK_5V);
    gd_eval_voltage_ok_off(VOLTAGE_OK_13V); // 经MOS：拉低使MOS切断，结果变高(异常)
    voltage_nged_flag = 1;
    
}

