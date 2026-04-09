#include "gd32e230c_eval.h"
#include "gd32e23x_adc.h"
#include "gd32e23x_dma.h"

#include <stdint.h>
#include <stdio.h>

/* 内部数据缓冲区 (由 DMA 异步填充) */
static uint16_t adc_raw_value[8];
static adc_channel_state_t adc_chan_states[8];

/* 默认 8 路全部为 2000-2100 范围，后续可通过 gd_eval_adc_set_threshold 修改 */
static adc_threshold_t adc_chan_thresholds[8] = {
    {2000, 2100}, {2000, 2100}, {2000, 2100}, {2000, 2100},
    {2000, 2100}, {2000, 2100}, {2000, 2100}, {2000, 2100}
};

/* 延迟函数弱定义 */
__attribute__((weak)) void delay_1ms(uint32_t count) { (void)count; }

/* ----------------- 硬件引脚映射表 ----------------- */

static const gd32e230c_pin_info_t LED_GPIO_PIN_INFO[LEDn] = {
    {LED1_PIN, LED1_GPIO_PORT, LED1_GPIO_CLK},
    {LED2_PIN, LED2_GPIO_PORT, LED2_GPIO_CLK},
};

static const gd32e230c_pin_info_t POWER_EN_PIN_INFO[POWER_EN_NUM] = {
    {POWER_EN_2V_PIN,  POWER_EN_2V_PORT,  POWER_EN_2V_CLK},
    {POWER_EN_5V_PIN,  POWER_EN_5V_PORT,  POWER_EN_5V_CLK},
    {POWER_EN_9V_PIN,  POWER_EN_9V_PORT,  POWER_EN_9V_CLK},
    {POWER_EN_36V_PIN, POWER_EN_36V_PORT, POWER_EN_36V_CLK},
    {POWER_EN_13V_PIN, POWER_EN_13V_PORT, POWER_EN_13V_CLK},
};

static const gd32e230c_pin_info_t ADC_PIN_INFO[ADC_CHANNEL_NUM] = {
    {GPIO_PIN_0, GPIOA, GPIOA}, {GPIO_PIN_1, GPIOA, GPIOA},
    {GPIO_PIN_2, GPIOA, GPIOA}, {GPIO_PIN_3, GPIOA, GPIOA},
    {GPIO_PIN_4, GPIOA, GPIOA}, {GPIO_PIN_5, GPIOA, GPIOA},
    {GPIO_PIN_6, GPIOA, GPIOA}, {GPIO_PIN_7, GPIOA, GPIOA},
};

/* 内部配置函数 */
static void gd_eval_adc_default_config(uint32_t channel_length);
static void gd_eval_dma_init(uint32_t number);

/* 获取原始 ADC 采样值 */
uint16_t gd_eval_adc_get_value(uint8_t index) 
{
    return (index < 8) ? adc_raw_value[index] : 0;
}

/* 获取通道状态结构体指针 */
adc_channel_state_t* gd_eval_adc_get_chan_state(uint8_t index)
{
    return (index < 8) ? &adc_chan_states[index] : NULL;
}

/* 设置通道阈值 */
void gd_eval_adc_set_threshold(uint8_t index, uint16_t min, uint16_t max)
{
    if (index < 8) {
        adc_chan_thresholds[index].min = min;
        adc_chan_thresholds[index].max = max;
    }
}

/* 获取当前配置阈值 */
adc_threshold_t gd_eval_adc_get_threshold(uint8_t index)
{
    adc_threshold_t invalid = {0, 0};
    return (index < 8) ? adc_chan_thresholds[index] : invalid;
}

/* 初始化特定 LED */
void gd_eval_led_init(led_typedef_enum lednum)
{
    rcu_periph_clock_enable(LED_GPIO_PIN_INFO[lednum].clk);
    gpio_mode_set(LED_GPIO_PIN_INFO[lednum].port, GPIO_MODE_OUTPUT, 
        GPIO_PUPD_NONE, LED_GPIO_PIN_INFO[lednum].pin);
    gpio_output_options_set(LED_GPIO_PIN_INFO[lednum].port, GPIO_OTYPE_PP, 
        GPIO_OSPEED_50MHZ, LED_GPIO_PIN_INFO[lednum].pin);
    GPIO_BC(LED_GPIO_PIN_INFO[lednum].port) = LED_GPIO_PIN_INFO[lednum].pin;
}

void gd_eval_led_on(led_typedef_enum lednum) 
{ 
    GPIO_BOP(LED_GPIO_PIN_INFO[lednum].port) = LED_GPIO_PIN_INFO[lednum].pin; 
}
void gd_eval_led_off(led_typedef_enum lednum) 
{ 
    GPIO_BC(LED_GPIO_PIN_INFO[lednum].port) = LED_GPIO_PIN_INFO[lednum].pin; 
}
void gd_eval_led_toggle(led_typedef_enum lednum) 
{ 
    GPIO_TG(LED_GPIO_PIN_INFO[lednum].port) = LED_GPIO_PIN_INFO[lednum].pin; 
}

/* 初始化 ADC 多通道扫描 (DMA 模式) */
void gd_eval_adc_init_multi_channel(uint8_t channels[], uint32_t length)
{
    gd_eval_adc_default_config(length);
    for (uint32_t i = 0; i < length; i++) 
    {
        gpio_mode_set(ADC_PIN_INFO[channels[i]].port, GPIO_MODE_ANALOG, 
            GPIO_PUPD_NONE, ADC_PIN_INFO[channels[i]].pin);
        adc_regular_channel_config(i, channels[i], ADC_SAMPLETIME_41POINT5);
    }

    adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
    adc_enable();
    delay_1ms(10);
    adc_calibration_enable();
    adc_dma_mode_enable();
    adc_software_trigger_enable(ADC_REGULAR_CHANNEL);
    gd_eval_dma_init(length);
}

/* 初始化电源使能引脚 */
void gd_eval_power_en_init(void)
{
    for (uint32_t i = 0; i < POWER_EN_NUM; i++) 
    {
        rcu_periph_clock_enable(POWER_EN_PIN_INFO[i].clk);
        gpio_mode_set(POWER_EN_PIN_INFO[i].port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, POWER_EN_PIN_INFO[i].pin);
        gpio_output_options_set(POWER_EN_PIN_INFO[i].port, 
            GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, POWER_EN_PIN_INFO[i].pin);
        
        /* 初始状态全部设为禁用 */
        gd_eval_power_en_set((power_en_typedef_enum)i, 0);
    }
}

/**
 * @brief 设置电源使能状态
 * @param en_idx: 通道索引
 * @param state: 1 为使能(OK), 0 为禁用(Not OK)
 * @note  极性处理:
 *        5V/13V: 高使能 (High=1)
 *        9V/2V/36V: 低使能 (Low=1)
 */
void gd_eval_power_en_set(power_en_typedef_enum en_idx, uint8_t state)
{
    uint8_t level = 0;
    switch(en_idx) 
    {
        case POWER_EN_5V:
        case POWER_EN_13V:
        {
            level = state; /* 高有效 */
            break;
        }
            
        case POWER_EN_9V:
        case POWER_EN_2V:
        case POWER_EN_36V:
        {
            level = !state; /* 低有效，当要使能(state=1)时，输出低(level=0) */
            break;
        }
        default: 
            return;
    }

    if(level) 
    {
        GPIO_BOP(POWER_EN_PIN_INFO[en_idx].port) = POWER_EN_PIN_INFO[en_idx].pin;
    } 
    else 
    {
        GPIO_BC(POWER_EN_PIN_INFO[en_idx].port) = POWER_EN_PIN_INFO[en_idx].pin;
    }
}

/* DMA 通道 0 初始化 (固定用于 ADC 搬运) */
static void gd_eval_dma_init(uint32_t number)
{
    rcu_periph_clock_enable(RCU_DMA);
    dma_parameter_struct dma_data_parameter;
    dma_deinit(DMA_CH0);

    dma_data_parameter.periph_addr  = (uint32_t)(&ADC_RDATA);
    dma_data_parameter.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_data_parameter.memory_addr  = (uint32_t)(adc_raw_value);
    dma_data_parameter.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_data_parameter.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_data_parameter.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_data_parameter.direction    = DMA_PERIPHERAL_TO_MEMORY;
    dma_data_parameter.number       = number;
    dma_data_parameter.priority     = DMA_PRIORITY_HIGH;
    dma_init(DMA_CH0, &dma_data_parameter);

    dma_circulation_enable(DMA_CH0);
    dma_channel_enable(DMA_CH0);
    dma_interrupt_enable(DMA_CH0, DMA_CHXCTL_FTFIE);
    nvic_irq_enable(DMA_Channel0_IRQn, 0U);
}

/* ADC 基础通用配置 (连续扫描模式) */
static void gd_eval_adc_default_config(uint32_t channel_length)
{
    rcu_periph_clock_enable(RCU_ADC);
    rcu_adc_clock_config(RCU_ADCCK_APB2_DIV6);
    adc_special_function_config(ADC_CONTINUOUS_MODE, ENABLE);
    adc_special_function_config(ADC_SCAN_MODE, ENABLE);
    adc_data_alignment_config(ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC_REGULAR_CHANNEL, channel_length);
}
