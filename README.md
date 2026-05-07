# AArch64 SVE Instruction Latency Tester

Measure latency and throughput of AArch64 SVE instructions with high precision.

## Quick Start

```bash
make
./sve_latency_test_optimized
```

## Usage

```bash
./sve_latency_test_optimized [options]

Options:
  --iterations <num>  Set iterations (default: 10000000, range: 1000-1000000000)
  --runs <num>        Set statistical runs (default: 10, range: 2-1000)
  --help              Show help
```

## Output Example

```
=== LDR Throughput ===
Latency: 0.138 ns (mean) ± 0.000 ns (SEM)
Range: [0.137, 0.139] ns
Relative error: ±0.17%
Throughput: 7255.94 M ops/sec

=== LDR Latency (Dependency Chain) ===
Latency: 1.872 ns (mean) ± 0.001 ns (SEM)
Range: [1.870, 1.875] ns
Relative error: ±0.03%
Throughput: 534.17 M ops/sec

=== FMLA (SVE) Latency ===
Latency: 0.870 ns (mean) ± 0.000 ns (SEM)
Range: [0.868, 0.872] ns
Relative error: ±0.04%
Throughput: 1149.95 M ops/sec
```

## Tested Instructions

- **LDR Throughput**: Independent loads (measure max parallelism)
- **LDR Latency**: Dependency chain loads (measure actual latency)
- **FMLA (SVE)**: Fused multiply-add with SVE vectors

## Code Structure

```
inst_latency/
├── main.c           - Entry point, config parsing
├── timer.c/h        - cntvct_el0 timer, loop calibration
├── stats.c/h        - Statistical analysis (mean, SEM)
├── benchmark.c/h    - Assembly test functions
├── test.c/h         - Test wrappers, memory allocation
└── Makefile         - Build configuration
```

## Requirements

- AArch64 CPU with SVE support
- GCC ≥9.0 or Clang ≥10.0
- Linux kernel ≥4.15

## Documentation

See [USER_MANUAL.md](USER_MANUAL.md) for detailed usage and methodology.

## License

For research and performance testing purposes.