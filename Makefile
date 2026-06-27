CC ?= cc
AR ?= ar
CFLAGS ?= -O3 -g -fPIC
CPPFLAGS += -Iinclude
WARN := -std=c11 -Wall -Wextra -Werror -pedantic
PREFIX ?= /usr/local

BUILD := build
LIB := $(BUILD)/libsscenc.a
SHLIB := $(BUILD)/libsscenc.so
CLI := $(BUILD)/sscenc
TEST := $(BUILD)/test_encoder

.PHONY: all clean install test

all: $(LIB) $(SHLIB) $(CLI)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/encoder.o: src/encoder.c include/ssc/encoder.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/sscenc.o: src/sscenc.c include/ssc/encoder.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/test_encoder.o: tests/test_encoder.c include/ssc/encoder.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(LIB): $(BUILD)/encoder.o
	$(AR) rcs $@ $^

$(SHLIB): $(BUILD)/encoder.o
	$(CC) -shared -Wl,-soname,libsscenc.so.0 $^ -o $@

$(CLI): $(BUILD)/sscenc.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST): $(BUILD)/test_encoder.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST)
	$(TEST)

install: all
	install -Dm755 $(CLI) $(DESTDIR)$(PREFIX)/bin/sscenc
	install -Dm644 $(LIB) $(DESTDIR)$(PREFIX)/lib/libsscenc.a
	install -Dm755 $(SHLIB) $(DESTDIR)$(PREFIX)/lib/libsscenc.so
	install -Dm644 include/ssc/encoder.h $(DESTDIR)$(PREFIX)/include/ssc/encoder.h

clean:
	rm -rf $(BUILD)
