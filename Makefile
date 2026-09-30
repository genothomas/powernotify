CC = clang
CFLAGS = -O2 -Wall -Wextra
FRAMEWORKS = -framework CoreFoundation -framework IOKit
TARGET = powernotify

.PHONY: all clean install

all: $(TARGET)

$(TARGET): src/powernotify.c
	$(CC) $(CFLAGS) $(FRAMEWORKS) $< -o $@

clean:
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/$(TARGET)
