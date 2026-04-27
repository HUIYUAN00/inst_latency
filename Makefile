CC = gcc
CFLAGS = -O3 -march=armv8-a+sve -Wall -Wextra -g
LDFLAGS = -lm

TARGET_ORIGINAL = sve_latency_test
TARGET_OPTIMIZED = sve_latency_test_optimized
SRC_ORIGINAL = sve_latency_test.c
SRC_OPTIMIZED = sve_latency_test_optimized.c

all: $(TARGET_ORIGINAL) $(TARGET_OPTIMIZED)

$(TARGET_ORIGINAL): $(SRC_ORIGINAL)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(TARGET_OPTIMIZED): $(SRC_OPTIMIZED)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET_ORIGINAL) $(TARGET_OPTIMIZED) analyze_precision error_budget

run-original: $(TARGET_ORIGINAL)
	./$(TARGET_ORIGINAL)

run-optimized: $(TARGET_OPTIMIZED)
	./$(TARGET_OPTIMIZED)

compare: $(TARGET_ORIGINAL) $(TARGET_OPTIMIZED)
	python3 compare_results.py

.PHONY: all clean run-original run-optimized compare