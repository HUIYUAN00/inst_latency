# AArch64 SVE 指令延迟测试工具 - 使用手册

## 目录

1. [项目概述](#项目概述)
2. [系统要求](#系统要求)
3. [快速开始](#快速开始)
4. [详细使用说明](#详细使用说明)
5. [结果解读指南](#结果解读指南)
6. [精度分析方法](#精度分析方法)
7. [常见问题](#常见问题)
8. [技术原理说明](#技术原理说明)
9. [性能优化建议](#性能优化建议)

---

## 项目概述

### 1.1 工具简介

本工具用于测量AArch64架构下指令的延迟和吞吐量。支持：

- **标量指令**: LDR内存访问吞吐量与延迟
- **SVE向量指令**: FMLA延迟测试

### 1.2 核心特性

| 特性 | 说明 |
|-----|------|
| **系统计数器计时** | cntvct_el0系统计数器（10ns精度） |
| **统计准确性** | SEM（标准误差）+ 相对误差百分比 |
| **完整输出** | 范围[min,max]、误差、精度警告 |
| **命令行配置** | --iterations和--runs参数 |

### 1.3 测试覆盖

| 测试类型 | 测试方法 | 输出指标 |
|---------|---------|---------|
| **吞吐量测试** | 独立操作序列，消除依赖 | 平均延迟、吞吐量、SEM误差 |
| **延迟测试** | 依赖链序列，强制串行 | 真实延迟、吞吐量、SEM误差 |

---

## 系统要求

### 2.1 硬件要求

| 要求项 | 规格 |
|-------|------|
| **处理器架构** | AArch64 (ARM64) |
| **SVE支持** | ARMv8-A + SVE扩展 |
| **内存** | ≥16MB（测试数组8MB） |
| **推荐CPU** | Neoverse N1/N2, Cortex-A76/A78/X1 |

### 2.2 软件要求

| 要求项 | 版本 |
|-------|------|
| **操作系统** | Linux (内核≥4.15) |
| **编译器** | GCC ≥9.0 或 Clang ≥10.0 |
| **C库** | glibc或musl |

---

## 快速开始

### 3.1 编译

```bash
# 进入项目目录
cd inst_latency

# 编译
make

# 清理
make clean
```

**编译输出**:
```
gcc -O3 -march=armv8-a+sve -Wall -Wextra -g -o sve_latency_test_optimized sve_latency_test_optimized.c -lm
```

### 3.2 运行

```bash
# 编译并运行
make run

# 或直接运行
./sve_latency_test_optimized

# 自定义参数
./sve_latency_test_optimized --iterations 10000000 --runs 10
```

### 3.3 查看结果

```
=== System Counter Timer ===
Method: cntvct_el0 (system counter)
Resolution: 10.00 ns per tick
Counter Frequency: 100.00 MHz
CPU Frequency: 2.200 GHz

=== LDR Throughput ===
Latency: 0.128 ns (mean) ± 0.001 ns (SEM)
Range: [0.128, 0.129] ns
Relative error: ±0.11%
Throughput: 7805.54 M ops/sec
```

---

## 详细使用说明

### 4.1 配置参数

命令行参数：

```bash
./sve_latency_test_optimized [options]

Options:
  --iterations <num>  Set iterations per test (default: 10000000)
  --runs <num>        Set statistical runs (default: 10)
  --help              Show help message
```

源码默认配置：

```c
// sve_latency_test_optimized.c

#define DEFAULT_ITERATIONS 10000000  // 每次测试迭代次数
#define UNROLL_FACTOR 32             // 循环展开因子
#define DEFAULT_NUM_RUNS 10          // 统计采样次数
#define ARRAY_SIZE (1024*1024)       // 测试数组大小
```

### 4.2 运行流程

```
初始化阶段:
  1. SVE能力检测
  2. 系统计数器初始化
  3. Cache预热

测试阶段:
  4. Loop开销校准（统计10次）
  5. 执行各项测试（LDR吞吐量、LDR延迟、FMLA延迟）
  6. 数据采集（每次测试10次采样）

输出阶段:
  7. 统计计算（mean, SEM, error_pct）
  8. 精度分析总结
  9. 结果展示
```

### 4.3 输出格式详解

#### 计时器配置输出

```
=== System Counter Timer ===
Method: cntvct_el0 (system counter)
Resolution: 10.00 ns per tick
Counter Frequency: 100.00 MHz
CPU Frequency: 2.200 GHz
```

#### Loop开销校准输出

```
=== Loop Overhead Calibration ===
Overhead: 142218.000 ns (mean) ± 10.729 ns (SEM)
Per-iteration: 0.014222 ns
Error contribution: 0.01%
```

#### 测试结果输出

```
=== LDR Throughput ===
Latency: 0.128 ns (mean) ± 0.000 ns (SEM)
Range: [0.128, 0.129] ns
Relative error: ±0.11%
Throughput: 7805.54 M ops/sec

[可选] WARNING: High measurement uncertainty (>10%)
```

#### 精度分析输出

```
=== Precision Analysis Summary ===
Timer: System Counter (cntvct_el0)
Resolution: 10.00 ns per tick
Counter Frequency: 100.00 MHz

Measurement methodology:
  Iterations: 10000000 per test
  Statistical runs: 10
  Effective resolution: 0.000001 ns
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

#### SEM的含义

```
SEM (标准误差): 均值的不确定性
  - 用于评估测量精度
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
  - cntvct: 10ns (系统计数器)

第2层: 累积测量精度
  - 10M迭代累积: 分辨率提升10000000倍
  - 有效分辨率: 10ns/10M = 0.000001ns

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
| **量化误差** | 10ns | 系统性 | 累积测量消除 |
| **Loop开销** | 0.01ns | 系统性 | 扣除校准值 |
| **Cache随机** | 0.05ns | 随机性 | 统计平均降低 |
| **Timer读取** | 1ns | 系统性 | ISB屏障消除 |

### 6.3 提升精度方法

#### 方法1: 增加迭代次数

```bash
# 增加迭代次数
./sve_latency_test_optimized --iterations 100000000

# 效果
量化误差: 从±0.001ns降至±0.0001ns
```

#### 方法2: 增加采样次数

```bash
# 增加采样次数
./sve_latency_test_optimized --runs 20

# 效果
SEM降低: √20/√10 = 1.41倍
```

#### 方法3: CPU亲和性绑定

```bash
# 绑定到CPU 0
taskset -c 0 ./sve_latency_test_optimized
```

#### 方法4: 提升优先级

```bash
# 最高优先级
sudo nice -n -20 ./sve_latency_test_optimized
```

---

## 常见问题

### 7.1 编译错误

**问题: SVE未支持**

```
错误: #error "SVE is not supported"
```

**解决**:
```bash
# 检查GCC版本
gcc --version  # 需要≥9.0

# 添加编译选项
gcc -march=armv8-a+sve sve_latency_test_optimized.c

# 或使用armclang
armclang -march=armv8.2-a+sve sve_latency_test_optimized.c
```

### 7.2 运行时错误

**问题: SVE Vector Length异常**

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

### 7.3 测试结果异常

**问题1: 延迟值过大**

```
LDR延迟: 50 ns (预期<2ns)
```

**可能原因**:
1. Cache/TLB未预热 → 检查warmup_cache
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
```bash
# 增加迭代次数
./sve_latency_test_optimized --iterations 100000000

# 增加采样次数
./sve_latency_test_optimized --runs 20
```

---

## 技术原理说明

### 8.1 时间累积原理

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

### 8.2 循环展开原理

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

### 8.3 依赖链测量原理

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

### 8.4 系统计数器原理

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

### 8.5 统计原理

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

### 9.1 测试环境优化

```bash
# 1. 禁用省电模式
sudo cpupower frequency-set -g performance

# 2. 绑定CPU核心
taskset -c 0 ./sve_latency_test_optimized

# 3. 提升优先级
sudo nice -n -20 ./sve_latency_test_optimized

# 4. 禁用其他干扰
# 建议在最小系统环境下测试
```

### 9.2 测试流程优化

```bash
# 推荐测试流程:

# 1. 环境准备
sudo cpupower frequency-set -g performance

# 2. 编译
make clean && make

# 3. 多次测试取稳定值
for i in {1..5}; do
    taskset -c 0 ./sve_latency_test_optimized | tee result_$i.log
    sleep 2
done
```

---

## 附录

### A. 命令速查表

```bash
# 编译
make                    # 编译程序
make clean              # 清理编译产物

# 运行
make run                # 编译并运行
./sve_latency_test_optimized  # 直接运行
./sve_latency_test_optimized --iterations 10000000 --runs 10  # 自定义参数

# CPU设置
sudo cpupower frequency-set -g performance  # 性能模式
taskset -c 0 ./sve_latency_test_optimized  # 绑定CPU0
```

### B. 文件清单

```
inst_latency/
├── main.c                          # 主程序入口
├── timer.c / timer.h               # 计时器模块
├── stats.c / stats.h               # 统计计算模块
├── benchmark.c / benchmark.h       # 性能测试模块
├── test.c / test.h                 # 测试包装模块
├── sve_latency_test_optimized      # 可执行文件
├── Makefile                        # 编译配置
├── USER_MANUAL.md                  # 本使用手册
└── .gitignore                      # Git忽略规则
```

### C. 参考资料

1. **ARM Architecture Reference Manual** - SVE指令集规范
2. **ARM System Counter Guide** - cntvct_el0计数器使用指南

---

**文档版本**: v3.0  
**更新日期**: 2026-05-06  
**适用版本**: sve_latency_test_optimized v1.0

---

## 反馈与支持

如有问题或建议，请查阅本手册FAQ章节。

---

*本工具仅用于性能测试和研究目的，结果受测试环境影响，请结合实际情况解读。*