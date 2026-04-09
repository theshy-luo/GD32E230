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

/* 初始化 ADC 多通道扫描 (DMA 模式), 自动把采样结果搬到内存里，之后软件几乎不用手动管采样过程
会让 ADC 不停地按48V-36V-9V-13V-1.4V-2V-5V-3.3V这个顺序循环采样, ADC每扫描完一轮，DMA就自动把结果写到 adc_raw_value[]
ADC 会按设定顺序不断循环采 8 路电压，DMA 自动把结果写进内存，DMA 中断再去做滤波和状态判定*/
void gd_eval_adc_init_multi_channel(uint8_t channels[], uint32_t length)
{
    gd_eval_adc_default_config(length);   //调用基础 ADC 配置
    for (uint32_t i = 0; i < length; i++) 
    {
        gpio_mode_set(ADC_PIN_INFO[channels[i]].port, GPIO_MODE_ANALOG, 
            GPIO_PUPD_NONE, ADC_PIN_INFO[channels[i]].pin);  //把对应 GPIO 切成模拟模式,因为 ADC 要读模拟电压，所以这些脚不能还是普通数字输入/输出模式。
         //i 是“它排在第几个采样”, channels[i] 是“采哪个 ADC 通道”, ADC_SAMPLETIME_41POINT5 是采样时间. 比如：第 0 个位置采 ADC_CH_48V
        adc_regular_channel_config(i, channels[i], ADC_SAMPLETIME_41POINT5);  //配置 ADC 的采样序列,DMA 后面写进数组的顺序，就跟这里配置的顺序一一对应。
    }

    //regular 通道组不依赖外部定时器或外部事件来触发,允许 regular 转换触发机制生效,配合软件触发，实际就是“由软件启动一次，然后 ADC 自己连续跑下去”
    adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_NONE); 
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
    adc_enable();  //先打开 ADC 模块
    delay_1ms(10);  //等 ADC 模拟部分稳定
    adc_calibration_enable();  //做校准，让转换结果更准
    adc_dma_mode_enable();  //ADC每次转换完成后，不是让CPU自己去读，而是交给DMA自动搬运。也就是说：ADC负责采样,DMA负责搬数据,CPU只在DMA中断时做高层处理,这就是这个工程效率比较高的原因
    adc_software_trigger_enable(ADC_REGULAR_CHANNEL);  //表示先由软件触发 regular 转换开始
    gd_eval_dma_init(length);  //配置 DMA,长度是 length
}

/* 初始化电源使能引脚 ，把所有电源控制脚准备好，并确保系统刚启动时所有受控电源都处于安全关闭状态
把 5 路“电源使能输出脚”统一初始化成 GPIO 推挽输出，每初始化完一路 GPIO，就立刻把这一路电源控制信号拉到“安全禁用态”，避免系统刚上电时误开启某路电源*/
void gd_eval_power_en_init(void)
{
    for (uint32_t i = 0; i < POWER_EN_NUM; i++)  //逐个处理 5 路输出，按映射表POWER_EN_PIN_INFO[i]，顺序对应 POWER_EN_2V / 5V / 9V / 36V / 13V
    {
        rcu_periph_clock_enable(POWER_EN_PIN_INFO[i].clk);  //打开这个 GPIO 端口的时钟
        gpio_mode_set(POWER_EN_PIN_INFO[i].port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, POWER_EN_PIN_INFO[i].pin);  //设成输出模式
        gpio_output_options_set(POWER_EN_PIN_INFO[i].port, 
            GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, POWER_EN_PIN_INFO[i].pin);  //设成推挽输出、高速.这些脚之后不再是输入或复用功能，而是由软件直接拉高/拉低，专门拿来发“使能/禁用”信号
        
        /* 初始状态全部设为禁用，传 0 的含义是“禁用”，底层函数会自动换算成该输出脚该写高还是写低。这样你就不用记每一路的硬件极性。 */
        /*把当前循环处理到的那一路电源使能脚”传进去,后面的 0是表示业务语义上的“禁用这路电源”。*/
        gd_eval_power_en_set((power_en_typedef_enum)i, 0); //(power_en_typedef_enum)i只是强类型转换，最后的值还是i对应的某电压输出口
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
