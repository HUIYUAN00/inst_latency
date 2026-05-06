CC = gcc
CFLAGS = -O3 -march=armv8-a+sve -Wall -Wextra -g
LDFLAGS = -lm

TARGET = sve_latency_test_optimized
SRC = sve_latency_test_optimized.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run