#include "gd32e23x_it.h"
#include "gd32e23x_dma.h"

#include "systick.h"
#include <stdint.h>

/* 必须加 volatile，防止编译器过度优化而读不到最新的值 */
volatile uint8_t adc_data_ready_flag = 0;

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

/*!
    \brief      this function handles ADC exception 
    \param[in]  none
    \param[out] none
    \retval     none
*/
void ADC_CMP_IRQHandler(void)
{

}

void DMA_Channel0_IRQHandler(void)
{
    /* 检查是否是“传输完成”中断标志 */
    if(dma_interrupt_flag_get(DMA_CH0, DMA_INT_FLAG_FTF))
    {
        /* 清除中断标志位（必须做，否则会反复触发中断） */
        dma_interrupt_flag_clear(DMA_CH0, DMA_INT_FLAG_FTF);
        
        /* 在这里写你的逻辑，比如通知主循环数据搬完了 */
        adc_data_ready_flag = 1; 
    }
}

void HardFault_Handler(void)
{
    while(1) 
    {
    }
}
