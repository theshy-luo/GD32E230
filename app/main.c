#include "gd32e230c_eval.h"
#include "gd32e23x.h"
#include "gd32e23x_it.h"
#include "systick.h"
#include <stdio.h>

/* 系统状态定义 */
typedef enum {
    SYS_IDLE,           /* 等待 48V */
    SYS_STARTUP_5V,     /* 使能 5V，等 OK (依赖: 48V) */
    SYS_STARTUP_9V,     /* 使能 9V，等 OK (依赖: 48V, 5V, 3.3V) */
    SYS_STARTUP_2V,     /* 使能 2V，等 OK (依赖: 48V, 5V, 3.3V, 9V) */
    SYS_STARTUP_13V,    /* 使能 13V，等 OK (依赖: 48V, 5V, 3.3V, 9V, 2V) */
    SYS_STARTUP_36V,    /* 使能 36V，等 OK (依赖: 48V, 5V, 3.3V, 9V, 2V, 13V) */
    SYS_NORMAL,         /* 正常监控 */
    SYS_FAULT           /* 故障锁定 */
} sys_state_t;

static sys_state_t g_system_state = SYS_IDLE;

/* 故障类型 */
typedef enum {
    FAULT_NONE,
    FAULT_48V,
    FAULT_OTHER
} fault_type_t;

static fault_type_t g_fault_type = FAULT_NONE;

void process_system_state(void);
uint8_t check_rail_ok(adc_channel_input_voltage ch);
void handle_fault_sequence(fault_type_t type);

int main(void)
{       
    systick_config();
    
    /* 初始化 8 路 ADC 通道 */
    uint8_t channels[8] = {
        ADC_CH_48V, ADC_CH_36V, ADC_CH_9V, ADC_CH_13V, 
        ADC_CH_V1P4, ADC_CH_2V, ADC_CH_5V, ADC_CH_V3P3
    };
    
    /* 预留 500ms 调试窗口 */
    delay_1ms(500);

    /* 1. 初始化外设 */
    gd_eval_led_init(LED1);
    gd_eval_led_init(LED2);
    gd_eval_power_en_init();
    
    /* 2. 初始化 ADC (DMA + 中断超采样) */
    gd_eval_adc_init_multi_channel(channels, 8);

    while (1) 
    {
        /* 等待 DMA 超采样轮询完成标志 (每 64 次触发一次) */
        if (adc_data_ready_flag) 
        {
            adc_data_ready_flag = 0;
            process_system_state(); 
        }
        
        /* 低功耗运行 */
        __WFI();
    }
}

/**
 * @brief 核心状态机逻辑
 */
void process_system_state(void)
{
    /* 故障锁定状态：不进行任何监测，保持信号输出 */
    if (g_system_state == SYS_FAULT) 
    {
        return;
    }

    /* 基础状态监测：LED1 反映 48V 状态 */
    if (check_rail_ok(ADC_CH_48V)) 
    {
        gd_eval_led_on(LED1);
    } 
    else 
    {
        gd_eval_led_off(LED1);
    }

    /* LED2 逻辑：监测除 48V 外的另外 7 路。任何一路异常都灭 */
    if (check_rail_ok(ADC_CH_36V) && check_rail_ok(ADC_CH_9V) && 
        check_rail_ok(ADC_CH_13V) && check_rail_ok(ADC_CH_V1P4) && 
        check_rail_ok(ADC_CH_2V) && check_rail_ok(ADC_CH_5V) && 
        check_rail_ok(ADC_CH_V3P3)) 
    {
        gd_eval_led_on(LED2);
    } 
    else 
    {
        gd_eval_led_off(LED2);
    }

    /* 系统逻辑处理 */
    switch (g_system_state) 
    {
        case SYS_IDLE:
        {
            /* 1. 等待 48V 输入正常 */
            if (check_rail_ok(ADC_CH_48V)) 
            {
                g_system_state = SYS_STARTUP_5V;
                gd_eval_power_en_set(POWER_EN_5V, 1);
            }
            break;
        }

        case SYS_STARTUP_5V:
        {
            /* 2. 5V 启动先决条件: 48V 必须持续 OK */
            if (!check_rail_ok(ADC_CH_48V)) 
            {
                /* 如果 48V 丢了，退回 IDLE 并关掉 5V (非故障，只是等待阶段) */
                gd_eval_power_en_set(POWER_EN_5V, 0);
                g_system_state = SYS_IDLE;
            } 
            else if (check_rail_ok(ADC_CH_5V)) 
            {
                /* 5V 正常后，准备开启 9V (依赖: 48, 5, 3.3) */
                if (check_rail_ok(ADC_CH_V3P3)) 
                {
                    gd_eval_power_en_set(POWER_EN_9V, 1);
                    g_system_state = SYS_STARTUP_9V;
                }
            }
            break;
        }

        case SYS_STARTUP_9V:
        {
            /* 3. 9V 启动确认 */
            if (!check_rail_ok(ADC_CH_48V) || !check_rail_ok(ADC_CH_5V) || !check_rail_ok(ADC_CH_V3P3)) 
            {
                /* 依赖项丢失，进入错误处理 (启动失败) */
                handle_fault_sequence(FAULT_OTHER);
            } else if (check_rail_ok(ADC_CH_9V)) {
                /* 开启 2V (依赖: 48, 5, 3.3, 9) */
                gd_eval_power_en_set(POWER_EN_2V, 1);
                g_system_state = SYS_STARTUP_2V;
            }
            break;
        }

        case SYS_STARTUP_2V:
        {
            /* 4. 2V 启动确认 */
            if (!check_rail_ok(ADC_CH_48V) || !check_rail_ok(ADC_CH_5V) || 
                !check_rail_ok(ADC_CH_V3P3) || !check_rail_ok(ADC_CH_9V)) 
            {
                handle_fault_sequence(FAULT_OTHER);
            } 
            else if (check_rail_ok(ADC_CH_2V)) 
            {
                /* 开启 13V (依赖: 48, 5, 3.3, 9, 2) */
                gd_eval_power_en_set(POWER_EN_13V, 1);
                g_system_state = SYS_STARTUP_13V;
            }
            break;
        }

        case SYS_STARTUP_13V:
        {
            /* 5. 13V 启动确认 */
            if (!check_rail_ok(ADC_CH_48V) || !check_rail_ok(ADC_CH_5V) || 
                !check_rail_ok(ADC_CH_V3P3) || !check_rail_ok(ADC_CH_9V) || !check_rail_ok(ADC_CH_2V))
            {
                handle_fault_sequence(FAULT_OTHER);
            } 
            else if (check_rail_ok(ADC_CH_13V)) 
            {
                /* 开启 36V (依赖: 48, 5, 3.3, 9, 2, 13) */
                gd_eval_power_en_set(POWER_EN_36V, 1);
                g_system_state = SYS_STARTUP_36V;
            }
            break;
        }

        case SYS_STARTUP_36V:
        {
            /* 6. 36V 启动确认 */
            if (!check_rail_ok(ADC_CH_48V) || !check_rail_ok(ADC_CH_5V) || 
                !check_rail_ok(ADC_CH_V3P3) || !check_rail_ok(ADC_CH_9V) || 
                !check_rail_ok(ADC_CH_2V) || !check_rail_ok(ADC_CH_13V))
            {
                handle_fault_sequence(FAULT_OTHER);
            } 
            else if (check_rail_ok(ADC_CH_36V)) 
            {
                /* 全部 OK，进入正常状态 */
                g_system_state = SYS_NORMAL;
            }
            break;
        }

        case SYS_NORMAL:
        {
            /* 7. 正常监测阶段逻辑 */
            /* 48V 不正常时的逻辑 */
            if (!check_rail_ok(ADC_CH_48V)) 
            {
                handle_fault_sequence(FAULT_48V);
                return;
            }
            /* 其他七路输入任意一路不正常逻辑 */
            if (!check_rail_ok(ADC_CH_36V) || !check_rail_ok(ADC_CH_9V) || 
                !check_rail_ok(ADC_CH_13V) || !check_rail_ok(ADC_CH_V1P4) || 
                !check_rail_ok(ADC_CH_2V) || !check_rail_ok(ADC_CH_5V) || 
                !check_rail_ok(ADC_CH_V3P3)) 
            {
                handle_fault_sequence(FAULT_OTHER);
            }
            break;
        }

        default:
            break;
    }
}

/**
 * @brief 检查指定通道是否 OK (处于 2000-2100 范围内并稳定)
 */
uint8_t check_rail_ok(adc_channel_input_voltage ch)
{
    adc_channel_state_t* state = gd_eval_adc_get_chan_state(ch);
    if(state) 
    {
        return state->is_ok;
    }
    return 0;
}

/**
 * @brief 故障处理序列
 * @param type: 故障类型
 */
void handle_fault_sequence(fault_type_t type)
{
    g_system_state = SYS_FAULT;
    g_fault_type = type;

    if (type == FAULT_48V) 
    {
        /* 48V不正常：只输出 36V 不 OK 信号 (36V使能设为禁用) */
        gd_eval_power_en_set(POWER_EN_36V, 0);
    } 
    else 
    {
        /* 其他七路异常：按照 36V -> 13V -> 2V 顺序输出信号 (禁用) */
        gd_eval_power_en_set(POWER_EN_36V, 0);
        delay_1ms(50); /* 微小间隔确保顺序特性 */
        gd_eval_power_en_set(POWER_EN_13V, 0);
        delay_1ms(50);
        gd_eval_power_en_set(POWER_EN_2V, 0);
    }
    
    /* 锁定之后，LED2 应该灭 (由于 check_rail_ok 判定逻辑) */
}
