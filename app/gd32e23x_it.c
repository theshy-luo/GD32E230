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
                state->sum += gd_eval_adc_get_value(i);
            }
        }
        
        /* 2. 判断是否达到超采样次数 (64次) */
        adc_sample_tick++;
        if(adc_sample_tick >= 64) 
        {
            adc_sample_tick = 0;
            
            for(uint8_t i = 0; i < 8; i++) 
            {
                adc_channel_state_t* state = gd_eval_adc_get_chan_state(i);
                if(!state) continue;
                
                /* 计算均值 */
                state->avg = (uint16_t)(state->sum >> 6); // sum / 64
                state->sum = 0;
                
                /* 稳定性判定 (2000-2100) */
                if(state->avg >= 2000 && state->avg <= 2100) 
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
