CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -pthread -Iinclude
LDFLAGS ?= -pthread
BIN=bin/otn_node
LIBSRC=src/epoll_reactor.c src/discovery.c src/control_channel.c src/node_daemon.c src/log.c
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
TARGET_CFLAGS := $(CFLAGS)
else
TARGET_CFLAGS := $(CFLAGS) -Wno-unused-function
endif

.PHONY: all clean test linux-integration
all: $(BIN)
bin:
	mkdir -p bin
$(BIN): bin src/main.c $(LIBSRC)
	$(CC) $(TARGET_CFLAGS) -o $@ src/main.c $(LIBSRC) $(LDFLAGS)
tests/test_control: tests/test_control.c src/control_channel.c include/control_channel.h
	$(CC) $(CFLAGS) -o $@ tests/test_control.c src/control_channel.c $(LDFLAGS)
tests/test_checksum: tests/test_checksum.c src/control_channel.c include/control_channel.h
	$(CC) $(CFLAGS) -o $@ tests/test_checksum.c src/control_channel.c $(LDFLAGS)
tests/test_state: tests/test_state.c src/node_daemon.c src/epoll_reactor.c src/discovery.c src/control_channel.c src/log.c
	$(CC) $(CFLAGS) -Wno-unused-function -o $@ tests/test_state.c src/node_daemon.c src/epoll_reactor.c src/discovery.c src/control_channel.c src/log.c $(LDFLAGS)
tests/test_log: tests/test_log.c src/log.c include/log.h
	$(CC) $(CFLAGS) -o $@ tests/test_log.c src/log.c $(LDFLAGS)
test: tests/test_control tests/test_checksum tests/test_state tests/test_log
	./tests/test_control
	./tests/test_checksum
	./tests/test_state
	./tests/test_log
	$(CC) $(CFLAGS) -fsyntax-only src/epoll_reactor.c src/discovery.c src/control_channel.c
	$(CC) $(CFLAGS) -Wno-unused-function -fsyntax-only src/node_daemon.c src/main.c src/log.c
linux-integration:
	@test "$$(uname -s)" = Linux || { echo "linux-integration requires Linux"; exit 1; }
	$(MAKE) all test
	./scripts/simulate.sh
clean:
	rm -rf bin tests/test_control tests/test_checksum tests/test_state tests/test_log
