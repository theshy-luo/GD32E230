/*!
    \file    gd32e230c_eval.h
    \brief   definitions for GD32E230C_EVAL's leds, keys and COM ports hardware
   resources

    \version 2026-02-04, V2.5.0, firmware for GD32E23x
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef GD32E230C_EVAL_H
#define GD32E230C_EVAL_H

#include "gd32e23x.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------- 逻辑类型定义 ----------------- */

/* LED 索引 */
typedef enum {
    LED1 = 0,
    LED2 = 1,
} led_typedef_enum;

/* 电源使能信号输出索引 */
typedef enum {
    POWER_EN_2V  = 0,
    POWER_EN_5V  = 1,
    POWER_EN_9V  = 2,
    POWER_EN_36V = 3,
    POWER_EN_13V = 4,
} power_en_typedef_enum;

/* ADC 通道逻辑映射 */
typedef enum {
    ADC_CH_48V  = 0,
    ADC_CH_36V  = 1,
    ADC_CH_9V   = 2,
    ADC_CH_13V  = 3,
    ADC_CH_V1P4 = 4,
    ADC_CH_2V   = 5,
    ADC_CH_5V   = 6,
    ADC_CH_V3P3 = 7,
} adc_channel_input_voltage;

/* 工业级采样状态结构体 */
typedef struct {
    uint32_t sum;            /* 累加值 (用于超采样) */
    uint16_t avg;            /* 滤波后的平均值 */
    uint8_t  stable_ok_cnt;  /* 稳定在范围内的计数器 */
    uint8_t  stable_err_cnt; /* 稳定在范围外的计数器 */
    uint8_t  is_ok;          /* 当前判定的 OK 状态 */
} adc_channel_state_t;

/* ADC 阈值配置结构体 */
typedef struct {
    uint16_t min;            /* 正常范围下限 */
    uint16_t max;            /* 正常范围上限 */
} adc_threshold_t;

/* 引脚基础信息结构 */
typedef struct {
    uint32_t pin;
    uint32_t port;
    uint32_t clk;
} gd32e230c_pin_info_t;

/* ----------------- 硬件资源常量 ----------------- */

#define LEDn            2U
#define POWER_EN_NUM    5U
#define ADC_CHANNEL_NUM 8U

/* LED 引脚定义 (PF组) */
#define LED1_PIN        GPIO_PIN_0
#define LED1_GPIO_PORT  GPIOF
#define LED1_GPIO_CLK   GPIOF

#define LED2_PIN        GPIO_PIN_1
#define LED2_GPIO_PORT  GPIOF
#define LED2_GPIO_CLK   GPIOF

/* 电压判定输出引脚定义 */
#define POWER_EN_2V_PIN   GPIO_PIN_1
#define POWER_EN_2V_PORT  GPIOB
#define POWER_EN_2V_CLK   GPIOB

#define POWER_EN_5V_PIN   GPIO_PIN_10
#define POWER_EN_5V_PORT  GPIOA
#define POWER_EN_5V_CLK   GPIOA

#define POWER_EN_9V_PIN   GPIO_PIN_9
#define POWER_EN_9V_PORT  GPIOA
#define POWER_EN_9V_CLK   GPIOA

#define POWER_EN_36V_PIN  GPIO_PIN_13
#define POWER_EN_36V_PORT GPIOA
#define POWER_EN_36V_CLK  GPIOA

#define POWER_EN_13V_PIN  GPIO_PIN_14
#define POWER_EN_13V_PORT GPIOA
#define POWER_EN_13V_CLK  GPIOA

/* ----------------- 访问接口 ----------------- */

/* ADC 数据获取 */
uint16_t gd_eval_adc_get_value(uint8_t index);
adc_channel_state_t* gd_eval_adc_get_chan_state(uint8_t index);
void gd_eval_adc_set_threshold(uint8_t index, uint16_t min, uint16_t max);
adc_threshold_t gd_eval_adc_get_threshold(uint8_t index);

/* LED 操作 */
void gd_eval_led_init(led_typedef_enum lednum);
void gd_eval_led_on(led_typedef_enum lednum);
void gd_eval_led_off(led_typedef_enum lednum);
void gd_eval_led_toggle(led_typedef_enum lednum);

/* ADC 初始化 (DMA模式) */
void gd_eval_adc_init_multi_channel(uint8_t *channels, uint32_t length);

/* 电源使能控制 (根据极性自动切换) */
void gd_eval_power_en_init(void);
void gd_eval_power_en_set(power_en_typedef_enum en_idx, uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* GD32E230C_EVAL_H */
