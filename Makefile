CC = gcc
CFLAGS = -Wall -pthread
TARGET = mts
SRCS = mts.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)