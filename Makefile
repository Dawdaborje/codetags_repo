CC := gcc
CFLAGS := -O2 -Wall -Wextra -std=c11
LDFLAGS :=
PREFIX := /usr/local

SRC_DIR := src
BUILD_DIR := build

SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC))

TARGET := codetags

SERVICE_NAME := codetags.service
USER_SYSTEMD_DIR := $(HOME)/.config/systemd/user
GLOBAL_CTAGSDIR := $(HOME)/.ctags
GLOBAL_REGISTRY := $(GLOBAL_CTAGSDIR)/registered_repos.txt
SYSTEM_BIN := $(PREFIX)/bin/$(TARGET)

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 0755 $(TARGET) $(SYSTEM_BIN)

uninstall:
	@echo "Stopping user service if running..."
	- systemctl --user stop $(SERVICE_NAME) || true
	- systemctl --user disable $(SERVICE_NAME) || true
	- systemctl --user daemon-reload || true
	@echo "Removing system binary..."
	- sudo rm -f $(SYSTEM_BIN)
	@echo "Removing global codetags registry and cache..."
	- rm -rf $(GLOBAL_CTAGSDIR)

# Full "nuke" clean: stop service, wipe build, wipe local repo state,
# wipe global registry, remove system binary
clean:
	@echo "[*] Stopping running codetags systemd user service..."
	- systemctl --user stop $(SERVICE_NAME) || true
	- systemctl --user disable $(SERVICE_NAME) || true
	- systemctl --user daemon-reload || true
	@echo "[*] Removing build artifacts..."
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "[*] Removing per-repo .ctags state (if present)..."
	- rm -rf .ctags codetags.md
	@echo "[*] Removing global registry in $(GLOBAL_CTAGSDIR)..."
	- rm -rf $(GLOBAL_CTAGSDIR)
	@echo "[*] Removing previously installed system binary..."
	- sudo rm -f $(SYSTEM_BIN)
	@echo "[*] Clean complete. You can now run your install script to reinstall fresh."

.PHONY: all install uninstall clean

