#!/usr/bin/env python3
"""
对比原版和优化版的测量结果
"""
import subprocess
import re

def parse_original_output(output):
    results = {}
    patterns = [
        ('LDR Throughput', r'=== Testing LDR Throughput ===.*?Latency \(throughput\): ([\d.]+) ns \(mean\), ([\d.]+) ns \(stddev\)'),
        ('LDR Latency', r'=== Testing LDR Latency.*?Latency \(dependency chain\): ([\d.]+) ns \(mean\), ([\d.]+) ns \(stddev\)'),
        ('FMLA Latency', r'=== Testing FMLA.*?Latency \(dependency chain\): ([\d.]+) ns \(mean\), ([\d.]+) ns \(stddev\)'),
    ]
    for name, pattern in patterns:
        match = re.search(pattern, output, re.DOTALL)
        if match:
            results[name] = {'mean': float(match.group(1)), 'stddev': float(match.group(2))}
    return results

def parse_optimized_output(output):
    results = {}
    patterns = [
        ('LDR Throughput', r'=== LDR Throughput ===.*?Latency: ([\d.]+) ns \(mean\) ± ([\d.]+) ns \(SEM\)'),
        ('LDR Latency', r'=== LDR Latency.*?Latency: ([\d.]+) ns \(mean\) ± ([\d.]+) ns \(SEM\)'),
        ('FMLA Latency', r'=== FMLA.*?Latency: ([\d.]+) ns \(mean\) ± ([\d.]+) ns \(SEM\)'),
    ]
    for name, pattern in patterns:
        match = re.search(pattern, output, re.DOTALL)
        if match:
            results[name] = {'mean': float(match.group(1)), 'sem': float(match.group(2))}
    return results

def compare_results(original, optimized):
    print("\n=== 结果对比分析 ===")
    print(f"{'测试项':<20} {'原版均值':<12} {'优化均值':<12} {'原版stddev':<12} {'优化SEM':<12} {'改进'}")
    print("-" * 80)
    
    for name in ['LDR Throughput', 'LDR Latency', 'FMLA Latency']:
        if name in original and name in optimized:
            orig_mean = original[name]['mean']
            opt_mean = optimized[name]['mean']
            orig_std = original[name]['stddev']
            opt_sem = optimized[name]['sem']
            
            diff_pct = abs(orig_mean - opt_mean) / orig_mean * 100
            
            print(f"{name:<20} {orig_mean:>8.3f} ns   {opt_mean:>8.3f} ns   "
                  f"{orig_std:>8.3f} ns   {opt_sem:>8.3f} ns   {diff_pct:.1f}%")

print("=== 运行测试对比 ===")

print("\n运行原版测试...")
orig_result = subprocess.run(['./sve_latency_test'], capture_output=True, text=True, timeout=60)
original = parse_original_output(orig_result.stdout)

print("\n运行优化版测试...")
opt_result = subprocess.run(['./sve_latency_test_optimized'], capture_output=True, text=True, timeout=60)
optimized = parse_optimized_output(opt_result.stdout)

print("\n原版输出摘要:")
print("-" * 40)
for line in orig_result.stdout.split('\n')[:20]:
    if '===' in line or 'Latency' in line or 'Timer' in line:
        print(line)

print("\n优化版输出摘要:")
print("-" * 40)
for line in opt_result.stdout.split('\n')[:30]:
    if '===' in line or 'Latency' in line or 'SEM' in line or 'Precision' in line:
        print(line)

compare_results(original, optimized)

print("\n=== 主要改进点 ===")
print("1. 统计方法: stddev → SEM (标准误差)")
print("   stddev = sqrt(variance) → SEM = stddev/sqrt(n)")
print("   更准确反映测量的不确定性")
print("\n2. 误差显示: 隐藏 → 显式相对误差百分比")
print("   让用户了解真实精度")
print("\n3. 测试次数: 5次 → 10次")
print("   SEM = stddev/sqrt(n)，增加样本数降低误差")
print("\n4. PMU支持: 尝试启用但失败时优雅降级")
print("   理论精度提升22x (0.45ns vs 10ns)")
print("\n5. 输出格式: 单一数值 → 范围+误差完整展示")
print("   Range [min, max] 展示测量分散性")