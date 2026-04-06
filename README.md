# GD32E23x GCC Template

这是一个基于 `GD32E23x_Firmware_Library_V2.5.0` 搭的最小 `GCC + CMake` 模板。
当前工程默认使用项目目录内自带的固件库副本，不再依赖下载目录的绝对路径。

## 目录结构

```text
gd32e23x_gcc_template/
├── app/
│   ├── gd32e23x_it.c
│   ├── gd32e23x_it.h
│   ├── gd32e23x_libopt.h
│   ├── main.c
│   ├── systick.c
│   └── systick.h
├── cmake/
│   └── arm-none-eabi-gcc.cmake
├── CMakeLists.txt
└── README.md
```

## 当前配置

- 工具链：`arm-none-eabi-gcc`
- 内核：`cortex-m23`
- 启动文件：官方 GCC 版本 `startup_gd32e23x.S`
- 链接脚本：官方 GCC 版本 `gd32e230x8_flash.ld`
- 示例功能：点亮并翻转 `GD32E230C-EVAL` 的 `LED1`

## 前提

你需要先安装：

- `Arm GNU Toolchain`
- `CMake`
- `Ninja`（推荐）

并确保这些命令可用：

```powershell
arm-none-eabi-gcc --version
cmake --version
ninja --version
```

## 配置

默认官方固件库路径写在 `CMakeLists.txt` 里：

```cmake
set(GD32_FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/GD32E23x_Firmware_Library_V2.5.0")
```

如果你后面换了路径，可以直接改这个变量，或者在配置时覆盖：

```powershell
cmake -S . -B build -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake ^
  -DGD32_FW_ROOT="你的固件库路径"
```

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

构建后会生成：

- `gd32e23x_gcc_template.elf`
- `gd32e23x_gcc_template.hex`
- `gd32e23x_gcc_template.bin`
- `compile_commands.json`

## clangd

这个模板已经开启：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

所以配置成功后，`build/compile_commands.json` 就能直接给 `clangd` 用。

如果你的编辑器希望在工程根目录找到它，可以手动复制或建立软链接。

## 后续怎么改成你自己的项目

1. 先把 `main.c` 改成自己的业务逻辑
2. 如果不是官方评估板，替换掉 `gd32e230c_eval.c/.h` 的依赖
3. 现在 `ADC / DMA / TIMER / GPIO / RCU / USART / MISC` 已经默认加入
4. 如果芯片容量不是 `x8`，把链接脚本改成对应型号
