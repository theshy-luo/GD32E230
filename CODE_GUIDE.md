# 代码导读手册：电源控制与监控系统

本手册旨在帮助开发者快速理解 GD32E230 电源监控工程的代码结构和运行机制。

---

## 1. 核心文件地图

| 文件 | 角色 | 核心内容 |
| :--- | :--- | :--- |
| **`app/main.c`** | **决策大脑** | 系统状态机 (`sys_state_t`)、顺序启动逻辑、故障关断逻辑。 |
| **`app/gd32e23x_it.c`** | **数据引擎** | DMA 中断处理、64 次超采样累加、电压稳定性 (`is_ok`) 判定。 |
| **`Utilities/gd32e230c_eval.c/h`** | **硬件接口** | GPIO 极性控制、ADC/DMA 初始化、通道状态结构体定义。 |

---

## 2. 数据流向：从物理电压到逻辑判定

了解本系统的关键在于理解电压是如何被“层层过滤”的：

### 第 1 步：采样 (Hardware)
ADC 配合 DMA 在后台循环扫描 8 路通道。
`gd_eval_adc_init_multi_channel()` 配置了这种“永不停歇”的采集模式。

### 第 2 步：累加滤波 (gd32e23x_it.c)
在 `DMA_Channel0_IRQHandler` 中：
1. 每搬运完一轮（8个数据），触发一次中断。
2. 将最新数据累加到 `adc_chan_states[i].sum`。
3. 每满 **64 次** 采样，进行一次平均值计算 (`sum >> 6`)。

### 第 3 步：持久化判定 (Stay Stable)
拿到均值后，系统并不立即采用，而是更新“稳定性计数器”：
- **OK 计数**：若均值在 2000-2100，`stable_ok_cnt++`。达到 **10** 次，`is_ok = 1`。
- **Error 计数**：若均值溢出，`stable_err_cnt++`。达到 **5** 次，`is_ok = 0`。
- 只要有一次反调，计数器立即清零。这保证了 `is_ok` 标志位极度可靠。

---

## 3. 逻辑控制：主状态机 (main.c)

主循环 `while(1)` 通过轮询 `adc_data_ready_flag` 定期执行 `process_system_state()`。

### 启动序列 (Startup Step-by-Step)
系统通过 `g_system_state` 维护当前步骤。
- 每个状态都是一个“死等”节点。
- **关键逻辑**：在切换到下一步前，会检查当前路及所有依赖路的 `is_ok`。
- **示例**：
  ```c
  case SYS_STARTUP_9V:
      if (check_rail_ok(ADC_CH_9V)) { // 9V 稳定了
          gd_eval_power_en_set(POWER_EN_2V, 1); // 开启下一步 2V
          g_system_state = SYS_STARTUP_2V;
      }
  ```

### 故障响应 (Fault Lock)
一旦进入 `SYS_FAULT` 状态，逻辑会执行一次 `handle_fault_sequence()`：
- 按照 **36V -> 13V -> 2V** 的顺序撤销使能信号（带 `delay_1ms(50)` 确保时序间隔）。
- 设置完成后，状态机不再跳转，系统锁定，直到重启。

---

## 4. 驱动层细节：极性控制

在 `gd32e230c_eval.c` 的 `gd_eval_power_en_set()` 中：
- 我们屏蔽了硬件上不同轨道的 Active High/Low 差异。
- **开发者只需要关注业务语义**：调用 `gd_eval_power_en_set(idx, 1)` 即代表“使能/开启”，底层会自动根据配置输出正确的高低电平。

---

## 5. 调试建议

如果您在实测中发现系统卡在某个启动步骤：
1. **查看均值**：通过仿真器查看 `adc_chan_states` 数组中对应通道的 `avg` 值。
2. **检查阈值**：确认硬件分压后的电压对应的 ADC 采样值是否真的在 `2000-2100` 范围内。
3. **观察状态**：查看 `g_system_state` 当前停留在哪个枚举值，对照 `main.c` 确认该步骤缺失的先决条件。
