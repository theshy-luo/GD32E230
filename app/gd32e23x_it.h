#ifndef GD32E23X_IT_H
#define GD32E23X_IT_H

#include <stdint.h>

/* 必须加 volatile，防止编译器过度优化而读不到最新的值 */
extern volatile uint8_t adc_data_ready_flag;

#endif
