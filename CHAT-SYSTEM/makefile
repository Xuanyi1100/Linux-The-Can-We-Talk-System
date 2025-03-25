all: server client

server:
	$(MAKE) -C chat-server

client:
	$(MAKE) -C chat-client

clean:
	$(MAKE) -C chat-server clean
	$(MAKE) -C chat-client clean

.PHONY: all server client clean