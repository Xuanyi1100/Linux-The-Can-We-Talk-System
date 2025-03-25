CC = gcc
CFLAGS = -Wall -Wextra
SRCS = src/chat-client.c
TARGET = bin/chat-client

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) -lpthread

clean:
	rm -f $(TARGET)

.PHONY: all clean