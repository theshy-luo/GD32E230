#include "gd32e230c_eval.h"
#include "gd32e23x_adc.h"
#include "gd32e23x_dma.h"

#include <stdint.h>
#include <stdio.h>

/* 内部数据缓冲区 (由 DMA 异步填充) */
static uint16_t adc_raw_value[8];   //是 DMA 自动写入的, ADC 每扫完一轮 8 个通道后，DMA 会把 8 路结果放进这个数组里
static adc_channel_state_t adc_chan_states[8];  //表示系统给 8 路 ADC 每一路都保存了一份状态信息

/*
 * 默认 8 路都使用统一窗口。
 * ignore：是否忽略（true：忽略这一路 false：恢复这一路参与判定）
 * 如果后续某一路不需要参与逻辑，可调用 gd_eval_adc_set_ignore() 屏蔽。
 */
static adc_threshold_t adc_chan_thresholds[8] = {
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false},
    {2000, 2100, false}
};

/* 延迟函数弱定义 */
__attribute__((weak)) void delay_1ms(uint32_t count) { (void)count; }

/* ----------------- 硬件引脚映射表 ----------------- */

static const gd32e230c_pin_info_t LED_GPIO_PIN_INFO[LEDn] = {
    {LED1_PIN, LED1_GPIO_PORT, LED1_GPIO_CLK},
    {LED2_PIN, LED2_GPIO_PORT, LED2_GPIO_CLK},
};

//5路输出映射表
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

/* 从 DMA 采样缓冲区里，读出某一路刚刚测到的那个数字
因为不是直接用单次采样判定，而是做了 64 次累加平均，所以这个函数返回的是“单次原始采样值”，后面再进入滤波流程。*/
uint16_t gd_eval_adc_get_value(uint8_t index) 
{
    return (index < 8) ? adc_raw_value[index] : 0;  //读取的数据来自这个 adc_raw_value[index]数组
}

/* 获取通道状态结构体指针,因为这一路状态不是一个简单数字，而是一整组会不断更新的数据。
去找到第 index 路 ADC 的状态，并把地址交给我，后面可以直接改adc_channel_state_t结构体里面的所有内容
这个函数不是取某一路 ADC 的采样值，而是取某一路 ADC 的整套运行状态*/
adc_channel_state_t* gd_eval_adc_get_chan_state(uint8_t index)
{
    return (index < 8) ? &adc_chan_states[index] : NULL;
}

/* 设置通道阈值， 给指定 ADC 通道设置一组新的正常判定范围 */
void gd_eval_adc_set_threshold(uint8_t index, uint16_t min, uint16_t max)
{
    if (index < 8) {
        adc_chan_thresholds[index].min = min;
        adc_chan_thresholds[index].max = max;
    }
}

/* 设置通道是否参与电压范围判定（忽略”不等于“不采样）， 设置该通道的 ignore 标志 
这个函数用来告诉系统，第 index 路 ADC 要不要参与 OK/NG 逻辑判断*/
void gd_eval_adc_set_ignore(uint8_t index, bool ignore)
{
    if (index < 8) {
        adc_chan_thresholds[index].ignore = ignore;
    }
}

/* 按通道索引，取出该通道当前使用的阈值配置,一整组配置，比如：{2000, 2100, false}
返回“结构体副本”而不是指针,后面直接读 threshold.min 就行。
“这个函数负责根据通道号，取回该通道当前的电压判定规则。”*/
adc_threshold_t gd_eval_adc_get_threshold(uint8_t index)  //返回值类型 adc_threshold_t 是一个结构体，包含 3 个字段：min,max,ignore
{
    adc_threshold_t invalid = {0, 0, false};  //先定义一个“无效默认值”,如果传入的 index 不合法，就返回这个默认结构体，而不是乱读数组内存。
    return (index < 8) ? adc_chan_thresholds[index] : invalid;  //判断索引是否合法
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

/*
 * 初始化 ADC 多通道扫描 (DMA 模式)。
 * 这里配置 ADC 按 {48V, 36V, 9V, 13V, 1.4V, 2V, 5V, 3.3V} 的顺序自动进行无限循环采样。
 * 采样结果会被 DMA 自动搬运到内存数组 adc_raw_value 中，完全不占用 CPU 时间。
 */
void gd_eval_adc_init_multi_channel(uint8_t channels[], uint32_t length)
{
    gd_eval_adc_default_config(length);
    for (uint32_t i = 0; i < length; i++) 
    {
        /* 将对应的引脚设置为模拟输入模式 */
        gpio_mode_set(ADC_PIN_INFO[channels[i]].port, GPIO_MODE_ANALOG, 
            GPIO_PUPD_NONE, ADC_PIN_INFO[channels[i]].pin);
        
        /* 配置规则组通道序列：第 i 个位置采指定的 ADC 通道 */
        adc_regular_channel_config(i, channels[i], ADC_SAMPLETIME_41POINT5);
    }

    /* 配置软件触发，不使用外部触发信号 */
    adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_NONE); 
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
    
    adc_enable();
    delay_1ms(10);
    adc_calibration_enable();
    adc_dma_mode_enable();
    
    /* 核心启动指令：由软件启动第一次转换，后续由于 DMA 循环模式，ADC 会一直自动采样下去 */
    adc_software_trigger_enable(ADC_REGULAR_CHANNEL);
    gd_eval_dma_init(length);
}

/* 
 * 初始化电源使能引脚。
 * 这里的核心思想是：先配置为输出，并立刻将所有引脚拉到“禁用电平”，确保上电瞬间系统是安全的。
 */
void gd_eval_power_en_init(void)
{
    for (uint32_t i = 0; i < POWER_EN_NUM; i++)
    {
        rcu_periph_clock_enable(POWER_EN_PIN_INFO[i].clk);
        gpio_mode_set(POWER_EN_PIN_INFO[i].port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, POWER_EN_PIN_INFO[i].pin);
        gpio_output_options_set(POWER_EN_PIN_INFO[i].port, 
            GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, POWER_EN_PIN_INFO[i].pin);
        
        /* 调用逻辑设置接口，屏蔽掉硬件上的高/低有效差异 */
        gd_eval_power_en_set((power_en_typedef_enum)i, 0); 
    }
}

/**
 *  设置电源使能状态
 *  en_idx: 通道索引
 *  state: 1 为使能(OK), 0 为禁用(Not OK)
 *  极性处理:把当前这一路电源输出设置成关闭状态，不管它的硬件极性是高有效还是低有效
 *        5V/13V: 高使能有效 (High=1)
 *        9V/2V/36V: 低使能有效 (Low=1)
 */
void gd_eval_power_en_set(power_en_typedef_enum en_idx, uint8_t state)   
{
    uint8_t level = 0;
    switch(en_idx) 
    {
        case POWER_EN_5V:
        case POWER_EN_13V:
        {
            level = state; /* 高有效,传 0 就会输出低电平，表示禁用 */
            break;
        }
            
        case POWER_EN_9V:
        case POWER_EN_2V:
        case POWER_EN_36V:
        {
            level = !state; /* 低有效，当要使能(state=1)时，输出低(level=0),传 0 就会输出高电平，表示禁用 */
            break;
        }
        default: 
            return;
    }

    if(level) 
    {
        //找到第 en_idx 路对应的端口和引脚，然后把这个引脚拉高。POWER_EN_PIN_INFO[en_idx].port表示这一路电源使能脚所在的 GPIO 端口. POWER_EN_PIN_INFO[en_idx].pin表示这一路对应的具体引脚位
        GPIO_BOP(POWER_EN_PIN_INFO[en_idx].port) = POWER_EN_PIN_INFO[en_idx].pin;  //GPIO_BOP(...)是GPIO 位操作寄存器，往这个寄存器写某一位，相当于把对应引脚直接置高。
    } 
    else 
    {
        GPIO_BC(POWER_EN_PIN_INFO[en_idx].port) = POWER_EN_PIN_INFO[en_idx].pin;
    }
}

/* DMA 通道 0 初始化 (固定用于 ADC 搬运), ADC 每扫描完一轮，DMA 就自动把结果写到 adc_raw_value[] */
static void gd_eval_dma_init(uint32_t number)
{
    rcu_periph_clock_enable(RCU_DMA);
    dma_parameter_struct dma_data_parameter;
    dma_deinit(DMA_CH0);

    dma_data_parameter.periph_addr  = (uint32_t)(&ADC_RDATA);  //外设地址指向 ADC_RDATA
    dma_data_parameter.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_data_parameter.memory_addr  = (uint32_t)(adc_raw_value);  //内存地址指向 adc_raw_value
    dma_data_parameter.memory_inc   = DMA_MEMORY_INCREASE_ENABLE; 
    dma_data_parameter.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_data_parameter.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_data_parameter.direction    = DMA_PERIPHERAL_TO_MEMORY;
    dma_data_parameter.number       = number;  //长度是 number
    dma_data_parameter.priority     = DMA_PRIORITY_HIGH;
    dma_init(DMA_CH0, &dma_data_parameter);

    dma_circulation_enable(DMA_CH0);  //开循环模式
    dma_channel_enable(DMA_CH0);  
    dma_interrupt_enable(DMA_CH0, DMA_CHXCTL_FTFIE);  //开 DMA 中断
    nvic_irq_enable(DMA_Channel0_IRQn, 0U);  //使能 DMA 通道
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

/**
 * @brief 通过 PA9 (USART0_TX) 发送带校验的编译信息 ($BOOT...*XX\r\n)
 * @note  采用类似 NMEA 0183 协议的 XOR 校验，方便主控端稳健解析
 */
void gd_uart_debug_send_info(void)
{
    /* 1. 开启时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    /* 2. 配置 PA9 为复用功能 USART0_TX */
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_9);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    /* 3. 配置 USART0 (115200, 8-N-1) */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
    usart_receive_config(USART0, USART_RECEIVE_DISABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);

    /* 4. 组装数据并计算 XOR 校验和 */
    char content[64];
    uint8_t checksum = 0;
    
    // 生成核心数据部分: BOOT,DATE=...,TIME=...
    int data_len = snprintf(content, sizeof(content), "BOOT,DATE=%s,TIME=%s", __DATE__, __TIME__);
    
    // 对核心内容进行异或
    for(int i = 0; i < data_len; i++) {
        checksum ^= (uint8_t)content[i];
    }
    
    // 拼装完整帧: $ + 内容 + * + 2位16进制校验 + \r\n
    char final_packet[128];
    int packet_len = snprintf(final_packet, sizeof(final_packet), "$%s*%02X\r\n", content, checksum);
    
    // 发送
    for(int i = 0; i < packet_len; i++) {
        usart_data_transmit(USART0, (uint8_t)final_packet[i]);
        while(RESET == usart_flag_get(USART0, USART_FLAG_TBE));
    }
    
    /* 等待最后一帧数据发送完毕 */
    while(RESET == usart_flag_get(USART0, USART_FLAG_TC));

    /* 5. 现场清理并恢复 PA9 为 GPIO 高电平 (禁用 9V) */
    usart_disable(USART0);
    rcu_periph_clock_disable(RCU_USART0);
    
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_9);
    GPIO_BOP(GPIOA) = GPIO_PIN_9; 
}

/*
 * [接收端参考伪代码 - 用于主控端解析 GD32 发出的引导信息]
 * -----------------------------------------------------------------------------------------
 * // 示例输入包: "$BOOT,DATE=Apr 10 2024,TIME=12:48:00*4F\r\n"
 * 
 * void process_gd32_boot_packet(char *packet_str) {
 *     char *start_ptr = strchr(packet_str, '$');
 *     char *end_ptr = strchr(packet_str, '*');
 *     
 *     if (start_ptr && end_ptr && (end_ptr > start_ptr)) {
 *         uint8_t calculated_xor = 0;
 *         
 *         // 1. 计算 $ 和 * 之间所有字符的异或值
 *         for (char *p = start_ptr + 1; p < end_ptr; p++) {
 *             calculated_xor ^= (uint8_t)(*p);
 *         }
 *         
 *         // 2. 解析字符串结尾的 2 位十六进制校验码
 *         unsigned int received_xor = 0;
 *         if (sscanf(end_ptr + 1, "%02X", &received_xor) == 1) {
 *             
 *             // 3. 比对校验值
 *             if (calculated_xor == (uint8_t)received_xor) {
 *                 // [校验成功] 数据完整，可以放心解析 DATE 和 TIME 字段
 *                 // 使用 sscanf(start_ptr, "$BOOT,DATE=%[^,],TIME=%[^*]", date_buf, time_buf);
 *             } else {
 *                 // [校验失败] 数据在传输过程中可能受到电磁干扰
 *             }
 *         }
 *     }
 * }
 * -----------------------------------------------------------------------------------------
 */
