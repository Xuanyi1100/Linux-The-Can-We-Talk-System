CC = gcc
CFLAGS = -Wall -Wextra
SRCS = src/chat-server.c
TARGET = bin/chat-server

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) -lpthread

clean:
	rm -f $(TARGET)

.PHONY: all clean