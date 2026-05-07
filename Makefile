CC = gcc
CFLAGS = -O3 -march=armv8-a+sve -Wall -Wextra -g
LDFLAGS = -lm

TARGET = sve_latency_test_optimized
SRCS = main.c timer.c stats.c benchmark.c test.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run