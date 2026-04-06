#include "gd32e23x.h"
#include "systick.h"

static volatile uint32_t delay_count;

/*!
    \brief      delay decrement
    \param[in]  none
    \param[out] none
    \retval     none
*/
void delay_decrement(void)
{
    if(delay_count > 0U) 
    {
        delay_count--;
    }
}

void systick_config(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
}

void delay_1ms(uint32_t count)
{
    delay_count = count;
    while(delay_count != 0U) {
    }
}
