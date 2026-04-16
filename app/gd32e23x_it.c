#include "gd32e23x_it.h"
#include "gd32e23x_dma.h"
#include "gd32e23x_i2c.h"
#include "gd32e230c_eval.h"
#include "systick.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* 必须加 volatile，防止编译器过度优化而读不到最新的值 */
volatile uint8_t adc_data_ready_flag = 0;
static uint32_t adc_sample_tick = 0;

/* --- 自定义日期与版本配置 --- */
#define CUSTOM_YEAR   25    /* 2025年 */
#define CUSTOM_MONTH  10    /* 10月 */
#define CUSTOM_DAY    30    /* 30号 */

#define VERSION_A     0     /* 版本号: 0.1.2.5 */
#define VERSION_B     1
#define VERSION_C     2
#define VERSION_D     5
/* -------------------------- */

void NMI_Handler(void)
{
}

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SysTick_Handler(void)
{
    delay_decrement();
}

/*
 * DMA 通道 0 中断处理函数
 * -----------------------------------------------------------------------------------------
 * 这是整个系统的数据引擎。ADC 每轮扫描完 8 个通道后，DMA 会触发一次 FTF (传输完成) 中断。
 * 我们在这里实现两级滤波逻辑：
 * 1. [超采样累加]：连续累加 64 轮 ADC 扫描值。
 * 2. [均值判定]：每 64 轮计算一次均数，并根据各通道独立阈值进行稳定性验证。
 * -----------------------------------------------------------------------------------------
 */
void DMA_Channel0_IRQHandler(void)
{
    /* 检查并清除“传输完成”中断标志 */
    if(dma_interrupt_flag_get(DMA_CH0, DMA_INT_FLAG_FTF))
    {
        dma_interrupt_flag_clear(DMA_CH0, DMA_INT_FLAG_FTF);
        
        /* 1. 累加 8 路采样值 */
        for(uint8_t i = 0; i < 8; i++) 
        {
            adc_channel_state_t* state = gd_eval_adc_get_chan_state(i);
            if(state) 
            {
                state->sum += gd_eval_adc_get_value(i);  //把第 i 路当前最新一次 ADC 原始值取出来，累加到 sum 里
            }
        }
        
        /* 2. 判断是否达到超采样次数 (64次) */
        adc_sample_tick++;
        if(adc_sample_tick >= 64) 
        {
            adc_sample_tick = 0;
            
            /*
             * 这里才真正把“64 次原始采样”折算成一次业务可用结果。
             * 主循环看到的 is_ok/avg，都是在这一步统一更新后的快照。
             */
            for(uint8_t i = 0; i < 8; i++) 
            {
                adc_channel_state_t* state = gd_eval_adc_get_chan_state(i);
                if(!state) continue;
                
                /* 计算均值 */
                state->avg = (uint16_t)(state->sum >> 6); // sum / 64
                state->sum = 0;
                
                /* 获取本通道独立的阈值配置,DMA 中断在处理第 i 路 ADC 时，先把这一路的阈值配置取出来，再用threshold.min.max.ignore去决定这一路怎么判定 is_ok */
                adc_threshold_t threshold = gd_eval_adc_get_threshold(i);
                if (threshold.ignore)
                {
                    /*
                     * ignore 只跳过当前通道，不能提前退出中断。
                     * 否则后续通道不会更新，adc_data_ready_flag 也不会置位，
                     * 主状态机将卡在旧数据上。
                     */
                    state->stable_ok_cnt = 0;
                    state->stable_err_cnt = 0;
                    state->is_ok = 1;
                    continue;
                }
                
                /*
                 * 稳定性判定:
                 * 1. 先判断均值是否落在该通道窗口内
                 * 2. 再用持续计数器过滤抖动
                 * 3. 只有“连续多次”满足条件才更新 is_ok
                 */
                if(state->avg >= threshold.min && state->avg <= threshold.max) 
                {
                    if(state->stable_ok_cnt < 255) state->stable_ok_cnt++;
                    state->stable_err_cnt = 0;
                    
                    /* 连续 10 次 OK 判定为有效 */
                    if(state->stable_ok_cnt >= 10) 
                    {
                        state->is_ok = 1;
                    }
                } 
                else 
                {
                    if(state->stable_err_cnt < 255) state->stable_err_cnt++;
                    state->stable_ok_cnt = 0;
                    
                    /* 连续 5 次 异常判定为失效 */
                    if(state->stable_err_cnt >= 5) 
                    {
                        state->is_ok = 0;
                    }
                }
            }
            
            /* 通知主循环均值已更新 */
            adc_data_ready_flag = 1; 
        }
    }
}

void HardFault_Handler(void)
{
    while(1) 
    {
    }
}

/**
 * @brief I2C0 事件中断处理函数
 * -----------------------------------------------------------------------------------------
 * 当主机发起 I2C 读操作时，此函数负责将 8 路 ADC 的 16 字节数据发送出去。
 * -----------------------------------------------------------------------------------------
 */
void I2C0_EV_IRQHandler(void)
{
    static uint8_t i2c_current_reg = 0x00;  /* 默认为电压读取 */
    static uint8_t tx_idx = 0;
    static uint8_t i2c_tx_buf[128];
    static uint8_t i2c_tx_len = 0;

    /* [地址匹配中断] - 准备数据发送阶段 */
    if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_ADDSEND))
    {
        /* 清除 ADDSEND 标志位 */
        i2c_flag_get(I2C0, I2C_FLAG_ADDSEND);
        i2c_flag_get(I2C0, I2C_FLAG_I2CBSY);

        /* 如果是读请求 (从机 -> 主机) */
        if(i2c_flag_get(I2C0, I2C_FLAG_TR) == SET)
        {
            tx_idx = 0;
            if(i2c_current_reg == 0x01)
            {
                /* 准备 8 字节日期与版本信息数据包 (使用自定义宏) */
                i2c_tx_buf[0] = (uint8_t)CUSTOM_YEAR;
                i2c_tx_buf[1] = (uint8_t)CUSTOM_MONTH;
                i2c_tx_buf[2] = (uint8_t)CUSTOM_DAY;
                i2c_tx_buf[3] = (uint8_t)VERSION_A;
                i2c_tx_buf[4] = (uint8_t)VERSION_B;
                i2c_tx_buf[5] = (uint8_t)VERSION_C;
                i2c_tx_buf[6] = (uint8_t)VERSION_D;
                i2c_tx_buf[7] = 0; /* 结束符 */
                i2c_tx_len = 8;
            }
            else
            {
                /* 默认：准备 16 字节电压数据 */
                for(uint8_t i = 0; i < 8; i++) 
                {
                    adc_channel_state_t* state = gd_eval_adc_get_chan_state(i);
                    uint16_t val = (state != NULL) ? state->avg : 0;
                    i2c_tx_buf[i * 2]     = (uint8_t)(val & 0xFF);
                    i2c_tx_buf[i * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
                }
                i2c_tx_len = 16;
            }
        }
    }
    /* [接收寄存器非空中断] - 捕获主控发来的指令 (Register Address) */
    else if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_RBNE))
    {
        i2c_current_reg = (uint8_t)i2c_data_receive(I2C0);
    }
    /* [发送寄存器空中断] - 按字节发送缓冲区内容 */
    else if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_TBE))
    {
        if(tx_idx < i2c_tx_len)
        {
            i2c_data_transmit(I2C0, i2c_tx_buf[tx_idx++]);
        }
        else
        {
            i2c_data_transmit(I2C0, 0x00);
        }
    }
    /* [停止信号中断] */
    else if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_STPDET))
    {
        i2c_flag_get(I2C0, I2C_FLAG_STPDET);
        i2c_enable(I2C0);
    }
}

/**
 * @brief I2C0 错误中断处理函数
 */
void I2C0_ER_IRQHandler(void)
{
    if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_BERR))
    {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_BERR);
    }
    if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_AERR))
    {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_AERR);
    }
    if(i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_OUERR))
    {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_OUERR);
    }
}
