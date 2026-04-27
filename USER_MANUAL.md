# AArch64 SVE 指令延迟测试工具 - 使用手册

## 目录

1. [项目概述](#项目概述)
2. [系统要求](#系统要求)
3. [快速开始](#快速开始)
4. [详细使用说明](#详细使用说明)
5. [结果解读指南](#结果解读指南)
6. [精度分析方法](#精度分析方法)
7. [高级配置](#高级配置)
8. [常见问题](#常见问题)
9. [技术原理说明](#技术原理说明)
10. [性能优化建议](#性能优化建议)

---

## 项目概述

### 1.1 工具简介

本工具用于精确测量AArch64架构下SVE（Scalable Vector Extension）指令的延迟和吞吐量。支持：

- **标量指令**: LDR/STR 内存访问
- **SVE向量指令**: LD1W/ST1W/FMLA/FCMLA
- **预取指令**: PRFM（多种hint类型）

### 1.2 核心特性

| 特性 | 说明 |
|-----|------|
| **高精度计时** | PMU cycles计数器优先（0.45ns精度），cntvct备选（10ns精度） |
| **统计准确性** | SEM（标准误差）+ 相对误差百分比，真实反映测量不确定性 |
| **智能降级** | PMU不可用时自动切换cntvct，保证测试正常运行 |
| **完整输出** | 范围[min,max]、误差、精度警告，避免误导性结果 |
| **CPU频率校准** | 动态测量实际CPU频率，消除DVFS/温度影响 |

### 1.3 测试覆盖

| 测试类型 | 测试方法 | 输出指标 |
|---------|---------|---------|
| **吞吐量测试** | 独立操作序列，消除依赖 | 平均延迟、吞吐量、SEM误差 |
| **延迟测试** | 依赖链序列，强制串行 | 真实延迟、吞吐量、SEM误差 |
| **预取测试** | PRFM不同hint类型 | Issue时间、吞吐量 |

---

## 系统要求

### 2.1 硬件要求

| 要求项 | 规格 |
|-------|------|
| **处理器架构** | AArch64 (ARM64) |
| **SVE支持** | ARMv8-A + SVE扩展 |
| **PMU支持** | 可选，建议启用（精度提升22x） |
| **内存** | ≥16MB（测试数组8MB） |
| **推荐CPU** | Neoverse N1/N2, Cortex-A76/A78/X1 |

### 2.2 软件要求

| 要求项 | 版本 |
|-------|------|
| **操作系统** | Linux (内核≥4.15) |
| **编译器** | GCC ≥9.0 或 Clang ≥10.0 |
| **C库** | glibc或musl |
| **Python** | ≥3.6（对比工具，可选） |

### 2.3 权限要求

```bash
# PMU访问权限检查（可选，提升精度）
cat /proc/sys/kernel/perf_event_paranoid
# 输出值:
#   ≥2: PMU完全禁用（仅cntvct可用）
#   1:  部分PMU功能可用
#   0:  PMU完全启用（推荐）
#  -1:  无限制（最理想）

# 启用PMU访问（需要root权限）
sudo echo 0 > /proc/sys/kernel/perf_event_paranoid
```

---

## 快速开始

### 3.1 编译

```bash
# 克隆或进入项目目录
cd inst_latency

# 编译所有版本
make all

# 仅编译优化版（推荐）
make sve_latency_test_optimized

# 仅编译原版（对比）
make sve_latency_test
```

**编译输出**:
```
gcc -O3 -march=armv8-a+sve -Wall -Wextra -g -o sve_latency_test sve_latency_test.c -lm
gcc -O3 -march=armv8-a+sve -Wall -Wextra -g -o sve_latency_test_optimized sve_latency_test_optimized.c -lm
```

### 3.2 运行

```bash
# 运行优化版（推荐）
make run-optimized

# 或直接运行
./sve_latency_test_optimized

# 运行原版（对比）
make run-original
./sve_latency_test
```

### 3.3 查看结果

```bash
# 实时查看输出
./sve_latency_test_optimized

# 对比分析
make compare
python3 compare_results.py
```

---

## 详细使用说明

### 4.1 命令行参数

目前工具通过源码宏配置，未来版本将支持命令行参数。

**当前配置方式**（修改源码）:
```c
// sve_latency_test_optimized.c

#define ITERATIONS 10000000      // 每次测试迭代次数
#define UNROLL_FACTOR 32         // 循环展开因子
#define NUM_RUNS 10              // 统计采样次数
#define ARRAY_SIZE (1024*1024)   // 测试数组大小
```

### 4.2 运行流程

```
初始化阶段:
  1. SVE能力检测
  2. PMU权限检查 → 选择计时器模式
  3. CPU频率校准
  4. Cache预热

测试阶段:
  5. Loop开销校准（统计10次）
  6. 执行各项测试（LDR/STR/SVE指令）
  7. 数据采集（每次测试10次采样）

输出阶段:
  8. 统计计算（mean, SEM, error_pct）
  9. 精度分析总结
  10. 结果展示
```

### 4.3 输出格式详解

#### 计时器配置输出

```
=== High-Precision Timer (PMU Cycles) ===  ← PMU可用时
Status: ENABLED
Method: pmccntr_el0 (CPU cycle counter)
Resolution: 0.455 ns per cycle           ← 高精度
CPU Frequency: 2.200 GHz (calibrated)
Precision Improvement: 22x vs system counter

或

=== Standard Timer (System Counter) ===   ← PMU不可用时
Status: PMU unavailable, using fallback
Method: cntvct_el0 (system counter)
Resolution: 10.00 ns per tick             ← 标准精度
Counter Frequency: 100.00 MHz
CPU Frequency: 2.200 GHz
```

#### Loop开销校准输出

```
=== Loop Overhead Calibration ===
Overhead: 142218.000 ns (mean) ± 10.729 ns (SEM)
Per-iteration: 0.014222 ns                ← 每迭代开销
Error contribution: 0.01%                 ← 对测量误差贡献
```

#### 测试结果输出

```
=== LDR Throughput ===
Latency: 0.128 ns (mean) ± 0.000 ns (SEM) ← 均值+标准误差
Range: [0.128, 0.129] ns                  ← 测量范围
Relative error: ±0.11%                    ← 相对误差百分比
Throughput: 7805.54 M ops/sec             ← 吞吐量

[可选] WARNING: High measurement uncertainty (>10%) ← 精度警告
```

#### 精度分析输出

```
=== Precision Analysis Summary ===
Timer precision comparison:
  PMU cycles: 0.455 ns (current)          ← 当前计时器精度
  cntvct: 10.00 ns (alternative)          ← 备选精度
  Improvement: 22x                        ← 精度倍数

Measurement methodology:
  Iterations: 10000000 per test           ← 迭代次数
  Accumulation: 2.00 ms per test          ← 累积时间
  Statistical runs: 10                    ← 统计采样数
  Effective resolution: 0.000001 ns       ← 有效分辨率
```

---

## 结果解读指南

### 5.1 核心指标说明

#### 均值 vs 标准误差

| 指标 | 含义 | 用途 |
|-----|------|------|
| **mean** | 10次测量的平均值 | 代表测量结果 |
| **SEM** | 标准误差=stddev/√n | 代表均值的不确定性 |
| **Range** | [最小值, 最大值] | 观察数据分散程度 |
| **error_pct** | 相对误差=SEM/mean×100% | 评估测量可靠性 |

#### SEM vs stddev的区别

```
stddev (标准差): 单次测量的分散程度
  - 用于评估重复性
  - 原版使用，但误导（显示0实际误差大）

SEM (标准误差): 均值的不确定性
  - 用于评估测量精度
  - 优化版使用，更准确
  - SEM = stddev / sqrt(n)
  - 增加采样次数可降低SEM
```

#### 示例解读

**良好测量（误差<5%）**:
```
Latency: 1.873 ns ± 0.001 ns (SEM)
Relative error: ±0.03%  ← 可信度高
```

**边缘测量（误差5-10%）**:
```
Latency: 0.214 ns ± 0.002 ns (SEM)
Relative error: ±0.9%   ← 可信度中等
```

**警告测量（误差>10%）**:
```
Latency: 0.128 ns ± 0.013 ns (SEM)
Relative error: ±10.2%  ← 可信度低
WARNING: High measurement uncertainty (>10%)
```

### 5.2 吞吐量 vs 延迟

| 测试类型 | 测量内容 | 适用场景 |
|---------|---------|---------|
| **吞吐量测试** | 理论最大操作速率 | 性能上限评估、带宽计算 |
| **延迟测试** | 单操作真实执行时间 | 程序执行时间估算、响应延迟 |

**示例**:
```
LDR吞吐量: 7805 M ops/sec = 每周期可执行~3.5条LDR
LDR延迟:   1.873 ns = 单次依赖链LDR耗时约4周期
```

### 5.3 数值合理性检查

#### 吞吐量上限

| CPU | 理论上限 | 合理范围 |
|-----|---------|---------|
| **2.2GHz单核** | 2200 M ops/sec | 1000-8000 M ops/sec |
| **超标量执行** | 多指令并行 | 吞吐量>频率表示并行 |

#### 延迟范围

| 指令类型 | 合理延迟 | 异常情况 |
|---------|---------|---------|
| **L1 Cache LDR** | 0.1-0.5 ns | >1ns可能L2/内存 |
| **依赖链LDR** | 1-5 ns | >10ns需检查数据链 |
| **FMLA SVE** | 0.5-2 ns | >5ns可能含其他开销 |

---

## 精度分析方法

### 6.1 精度层级

```
第1层: 计时器硬件精度
  - PMU cycles: 0.45ns (CPU周期)
  - cntvct: 10ns (系统计数器)

第2层: 累积测量精度
  - 10M迭代累积: 分辨率提升10000000倍
  - 有效分辨率: 0.45ns/10M = 0.000000045ns

第3层: 统计精度
  - 10次采样: SEM = stddev/√10
  - 相对误差: SEM/mean × 100%

第4层: 系统误差（不可消除）
  - Cache效应: ±0.05ns
  - TLB缺失: ±14ns
  - 分支预测: ±5ns
```

### 6.2 误差来源分析

| 误差来源 | 量级 | 特性 | 消除方法 |
|---------|------|------|---------|
| **量化误差** | 0.45ns或10ns | 系统性 | 累积测量消除 |
| **Loop开销** | 0.01ns | 系统性 | 扣除校准值 |
| **Cache随机** | 0.05ns | 随机性 | 统计平均降低 |
| **Timer读取** | 1ns | 系统性 | ISB屏障消除 |
| **DVFS波动** | ±9% | 随机性 | CPU频率校准 |

### 6.3 真实精度评估

**PMU可用环境**:
```
单次测量精度: 0.45ns
累积后精度: ±0.002ns (统计)
相对误差: <2% (对sub-ns测量可信)
```

**PMU不可用环境**:
```
单次测量精度: 10ns
累积后精度: ±0.05ns (统计+系统误差)
相对误差: ±40% (对sub-ns测量不可信)
```

### 6.4 提升精度方法

#### 方法1: 启用PMU

```bash
# 检查当前状态
cat /proc/sys/kernel/perf_event_paranoid

# 启用PMU（root权限）
sudo echo 0 > /proc/sys/kernel/perf_event_paranoid

# 验证PMU可用
perf stat -e cycles ls
```

#### 方法2: 增加迭代次数

```c
// 修改源码
#define ITERATIONS 100000000  // 从10M增加到100M

// 效果
量化误差: 从±0.05ns降至±0.005ns
相对误差: 从±40%降至±4%
```

#### 方法3: 增加采样次数

```c
// 修改源码
#define NUM_RUNS 20  // 从10增加到20

// 效果
SEM降低: √20/√10 = 1.41倍
```

#### 方法4: CPU亲和性绑定

```c
// 在main()开头添加
#include <sched.h>
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);  // 绑定到CPU 0
sched_setaffinity(0, sizeof(cpuset), &cpuset);
```

#### 方法5: 提升优先级

```c
// 在main()开头添加
#include <sys/resource.h>
setpriority(PRIO_PROCESS, 0, -20);  // 最高优先级
```

---

## 高级配置

### 7.1 计时器模式选择

```c
// sve_latency_test_optimized.c

typedef enum {
    TIMER_PMU_CYCLES,  // 强制使用PMU（PMU不可用会失败）
    TIMER_CNTVCT,      // 强制使用cntvct
    TIMER_AUTO         // 自动选择（PMU优先，备选cntvct）
} timer_mode_t;

static timer_mode_t g_timer_mode = TIMER_AUTO;  // 推荐默认
```

**模式说明**:
- `TIMER_AUTO`: 智能选择，PMU可用时用PMU，否则cntvct（推荐）
- `TIMER_PMU_CYCLES`: 仅PMU，PMU不可用时测试失败（高精度场景）
- `TIMER_CNTVCT`: 仅cntvct，兼容性最好（兼容性测试）

### 7.2 测试参数调优

```c
// 针对不同场景的推荐配置

// 场景1: 快速测试（开发调试）
#define ITERATIONS 1000000
#define NUM_RUNS 3

// 场景2: 标准测试（性能评估）
#define ITERATIONS 10000000
#define NUM_RUNS 10

// 场景3: 高精度测试（科研论文）
#define ITERATIONS 100000000
#define NUM_RUNS 20

// 场景4: 极限测试（硬件验证）
#define ITERATIONS 1000000000
#define NUM_RUNS 50
```

### 7.3 Cache预热强度

```c
// 当前实现
static void warmup_cache(void) {
    volatile uint64_t *dummy = aligned_alloc(64, 1024*1024);
    for (int i = 0; i < 131072; i++) dummy[i] = i;  // 1MB预热
    free((void*)dummy);
}

// 增强预热（消除Cold Cache影响）
static void warmup_cache_enhanced(void) {
    volatile uint64_t *dummy = aligned_alloc(64, 8*1024*1024);
    for (int i = 0; i < 1048576; i++) dummy[i] = i;  // 8MB预热
    for (int j = 0; j < 3; j++) {  // 多次预热
        for (int i = 0; i < 1048576; i++) dummy[i] += j;
    }
    free((void*)dummy);
}
```

### 7.4 输出详细度控制

```c
// 添加输出级别控制
typedef enum {
    OUTPUT_MINIMAL,    // 仅输出均值
    OUTPUT_STANDARD,   // 均值+SEM+误差%（当前）
    OUTPUT_DETAILED,   // 完整统计+分位数
    OUTPUT_VERBOSE     // 包含每次测量原始数据
} output_level_t;

// 在源码中添加控制开关
static output_level_t g_output_level = OUTPUT_STANDARD;
```

---

## 常见问题

### 8.1 编译错误

**问题1: SVE未支持**

```
错误: #error "SVE is not supported"
```

**解决**:
```bash
# 检查GCC版本
gcc --version  # 需要≥9.0

# 添加编译选项
gcc -march=armv8-a+sve sve_latency_test.c

# 或使用armclang
armclang -march=armv8.2-a+sve sve_latency_test.c
```

**问题2: PMU寄存器未定义**

```
错误: 'pmccntr_el0' undeclared
```

**解决**:
```bash
# GCC需要特定版本支持PMU寄存器名
gcc --version  # 需要≥10.0

# 使用数字编码替代（GCC<10）
asm volatile ("mrs %0, pmccntr_el0" : "=r"(cycles));
// 替代为
asm volatile ("mrs %0, s3_14_c15_c10_5" : "=r"(cycles));
```

### 8.2 运行时错误

**问题1: PMU不可用**

```
输出: PMU unavailable, using fallback
```

**解决**:
```bash
# 方法1: 修改系统权限（需要root）
sudo echo 0 > /proc/sys/kernel/perf_event_paranoid

# 方法2: 使用perf包装运行
perf stat -e cycles ./sve_latency_test_optimized

# 方法3: 使用cntvct模式继续测试
// 修改源码
g_timer_mode = TIMER_CNTVCT;
```

**问题2: SVE Vector Length异常**

```
输出: SVE Vector Length: 128 bits (预期256 bits)
```

**原因**: SVE向量长度可配置，128/256/512 bits均合法

**解决**:
```bash
# 检查CPU支持的最大向量长度
cat /proc/cpuinfo | grep "SVE"

# 运行时向量长度可查询
# 程序已自动检测，无需修改
```

### 8.3 测试结果异常

**问题1: 延迟值过大**

```
LDR延迟: 50 ns (预期<2ns)
```

**可能原因**:
1. Cache/TLB未预热 → 添加warmup_cache_enhanced()
2. 内存分配未对齐 → 使用aligned_alloc(64, size)
3. 数据链断裂 → 检查指针链构造逻辑

**问题2: 吞吐量过低**

```
LDR吞吐: 100 M ops/sec (预期>5000)
```

**可能原因**:
1. CPU频率降频 → 检查CPU频率
2. 系统负载过高 → 独占运行或提升优先级
3. 测试逻辑错误 → 检查循环展开是否正确

**问题3: 误差过大**

```
Relative error: ±50% (预期<5%)
```

**解决**:
```c
// 增加迭代次数
#define ITERATIONS 100000000

// 增加采样次数
#define NUM_RUNS 20

// 启用PMU（如果可用）
```

### 8.4 环境兼容性

**问题: 不同Linux发行版**

| 发行版 | GCC版本 | 编译选项 |
|-------|---------|---------|
| Ubuntu 20.04 | GCC 9.3 | `-march=armv8-a+sve` |
| CentOS 8 | GCC 8.5 | 需升级GCC或使用armclang |
| Arch Linux | GCC 11+ | 标准选项即可 |
| Debian 11 | GCC 10.2 | 标准选项即可 |

---

## 技术原理说明

### 9.1 时间累积原理

**基本原理**:
```
单次测量精度低 (10ns) → 累积大量测量 → 总时间精度高

示例:
  单次LDR延迟: 0.128ns (无法直接测量)
  10M次累积: 0.128ns × 10M = 1.28ms (可精确测量)
  计算延迟: 1.28ms / 10M = 0.128ns

有效精度提升:
  计时器精度 / 迭代次数 = 有效精度
  10ns / 10M = 0.000001ns
```

**数学证明**:
```
设计时器精度为δ，迭代次数为N

总时间T = N × τ + ε_quantization
其中τ为真实延迟，ε_quantization ≤ δ

测量延迟τ_measured = T / N = τ + ε_quantization/N

误差 = ε_quantization/N ≤ δ/N

结论: N次累积后，精度从δ提升到δ/N
```

### 9.2 循环展开原理

**目的**: 减少循环控制开销占比

```
未展开:
  for (i=0; i<10000000; i++) {
    ldr x0, [x0]      // 1次有效操作
    branch            // 循环开销
  }
  循环开销占比: 50%

展开32次:
  for (i=0; i<10000000/32; i++) {
    ldr x0, [x0]      // 32次有效操作
    ldr x0, [x0]
    ... (重复32次)
    branch            // 循环开销
  }
  循环开销占比: 1/32 = 3%
```

### 9.3 依赖链测量原理

**吞吐量测试 vs 延迟测试**:

```
吞吐量测试（独立操作）:
  ldr x1, [ptr]     // 独立
  ldr x2, [ptr+8]   // 独立，可并行
  ldr x3, [ptr+16]  // 独立，可并行
  ...
  测量: 最大并行度 = 吞吐量

延迟测试（依赖链）:
  ldr x0, [x0]      // x0依赖上一条结果
  ldr x0, [x0]      // 强制串行
  ldr x0, [x0]      // 无法并行
  ...
  测量: 单操作真实延迟 = 延迟
```

### 9.4 PMU计数器原理

**PMU cycles计数器**:
```
寄存器: pmccntr_el0
计数内容: CPU执行的周期数
精度: 1 cycle (0.45ns at 2.2GHz)
特性: 
  - 只增不减（除非手动清零）
  - 32位计数器，需处理溢出（返回值<<1扩展）
  - 需要权限启用（perf_event_paranoid≤0）
```

**cntvct系统计数器**:
```
寄存器: cntvct_el0
计数内容: 系统时间（与CPU频率无关）
精度: 1 tick (10ns at 100MHz cntfrq)
特性:
  - 固定频率，不受DVFS影响
  - 64位计数器，无溢出问题
  - 无需特殊权限，总是可用
```

### 9.5 统计原理

**标准误差(SEM)**:
```
定义: SEM = σ / √n
其中σ为样本标准差，n为样本数

含义: 均值的不确定性

示例:
  10次测量: stddev=0.1ns → SEM=0.1/√10=0.032ns
  100次测量: stddev=0.1ns → SEM=0.1/√100=0.010ns
  
结论: 增加采样次数可降低SEM，提升精度
```

**相对误差**:
```
定义: 相对误差 = SEM / mean × 100%

阈值:
  <5%: 高可信度
  5-10%: 中可信度
  >10%: 低可信度（需警告）

示例:
  mean=1.873ns, SEM=0.001ns → 相对误差=0.05%
  mean=0.128ns, SEM=0.013ns → 相对误差=10.2% (警告)
```

---

## 性能优化建议

### 10.1 测试环境优化

```bash
# 1. 禁用省电模式
sudo cpupower frequency-set -g performance

# 2. 禁用动态调频
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# 3. 绑定CPU核心
taskset -c 0 ./sve_latency_test_optimized

# 4. 提升优先级
sudo nice -n -20 ./sve_latency_test_optimized

# 5. 禁用其他干扰
# 建议在最小系统环境下测试
```

### 10.2 测试流程优化

```bash
# 推荐测试流程:

# 1. 环境准备
sudo echo 0 > /proc/sys/kernel/perf_event_paranoid
sudo cpupower frequency-set -g performance

# 2. 编译
make clean && make all

# 3. 多次测试取稳定值
for i in {1..5}; do
    taskset -c 0 ./sve_latency_test_optimized | tee result_$i.log
    sleep 2
done

# 4. 分析结果
python3 analyze_results.py result_*.log
```

### 10.3 数据处理建议

```python
# compare_results.py 增强版

import numpy as np

def analyze_stability(log_files):
    """分析多次测试的稳定性"""
    means = []
    sems = []
    
    for log in log_files:
        # 解析均值和SEM
        mean, sem = parse_result(log)
        means.append(mean)
        sems.append(sem)
    
    # 计算跨测试稳定性
    overall_mean = np.mean(means)
    overall_std = np.std(means)
    stability = overall_std / overall_mean * 100
    
    print(f"Overall mean: {overall_mean:.3f} ns")
    print(f"Stability: ±{stability:.2f}%")
    
    # 稳定性判断
    if stability < 1:
        print("✓ Excellent stability")
    elif stability < 5:
        print("✓ Good stability")
    else:
        print("⚠ Poor stability, check environment")
```

### 10.4 结果报告建议

```markdown
# 测试报告模板

## 测试环境
- CPU型号: [填写]
- CPU频率: [填写]
- SVE向量长度: [填写]
- 计时器模式: PMU/cntvct
- 系统版本: [填写]

## 测试结果

| 指令 | 延迟(ns) | 误差(%) | 吞吐量(M ops/s) |
|-----|---------|---------|----------------|
| LDR吞吐 | 0.128 | 0.11 | 7805 |
| LDR延迟 | 1.873 | 0.03 | 534 |
| FMLA延迟 | 0.897 | 0.05 | 1115 |

## 精度分析
- 计时器精度: [PMU: 0.45ns / cntvct: 10ns]
- 有效精度: [填写]
- 综合误差: [填写]

## 结论
[填写测试结论和分析]
```

---

## 附录

### A. 命令速查表

```bash
# 编译
make all                    # 编译所有版本
make sve_latency_test_optimized  # 仅编译优化版

# 运行
make run-optimized          # 运行优化版
make run-original           # 运行原版
make compare                # 对比分析

# 清理
make clean                  # 清理所有编译产物

# PMU权限
cat /proc/sys/kernel/perf_event_paranoid  # 查看权限
sudo echo 0 > /proc/sys/kernel/perf_event_paranoid  # 启用

# CPU设置
sudo cpupower frequency-set -g performance  # 性能模式
taskset -c 0 ./test          # 绑定CPU0
```

### B. 文件清单

```
inst_latency/
├── sve_latency_test.c              # 原版测试程序
├── sve_latency_test_optimized.c    # 优化版测试程序
├── Makefile                        # 编译配置
├── compare_results.py              # 结果对比工具
├── PRECISION_ANALYSIS_REPORT.md    # 精度分析报告
├── OPTIMIZATION_SUMMARY.md         # 优化总结
├── USER_MANUAL.md                  # 本使用手册
├── sve_latency_test                # 原版可执行文件
└── sve_latency_test_optimized      # 优化版可执行文件
```

### C. 参考资料

1. **ARM Architecture Reference Manual** - SVE指令集规范
2. **ARM PMU Guide** - 性能监控单元使用指南
3. **Linux Perf Documentation** - perf工具文档
4. **Intel/AMD Optimization Manual** - 微基准测试方法论（通用原理）

---

**文档版本**: v1.0  
**生成日期**: 2026-04-27  
**适用版本**: sve_latency_test_optimized v1.0

---

## 反馈与支持

如有问题或建议，请：
1. 查阅本手册FAQ章节
2. 查看PRECISION_ANALYSIS_REPORT.md了解精度细节
3. 查看OPTIMIZATION_SUMMARY.md了解技术改进

---

*本工具仅用于性能测试和研究目的，结果受测试环境影响，请结合实际情况解读。*