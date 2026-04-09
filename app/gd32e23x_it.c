#include "gd32e23x_it.h"
#include "gd32e23x_dma.h"
#include "gd32e230c_eval.h"
#include "systick.h"
#include <stdint.h>
#include <stddef.h>

/* 必须加 volatile，防止编译器过度优化而读不到最新的值 */
volatile uint8_t adc_data_ready_flag = 0;
static uint32_t adc_sample_tick = 0;

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

void DMA_Channel0_IRQHandler(void)
{
    /* 检查是否是“传输完成”中断标志 */
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
