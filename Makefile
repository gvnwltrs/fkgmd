##
# F*cking Markdown Reader
#
# @fkgmd.c
# @version 0.1

CC = gcc
CFLAGS = -O3 -Wall $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1)

SRC = src/fkgmd.c
TARGET = fkgmd

# Install paths (User-local by default to avoid sudo)
PREFIX ?= $(HOME)/.local
BINDIR = $(PREFIX)/bin
APPDIR = $(PREFIX)/share/applications

.PHONY: all clean install uninstall setup

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

setup:
	@echo "Detecting OS for dependency installation..."
	@if grep -qiE "debian|ubuntu|mint|pop" /etc/os-release; then \
		echo "Debian-based OS detected."; \
		sudo apt update && sudo apt install -y build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev; \
	elif grep -qi "void" /etc/os-release; then \
		echo "Void Linux detected."; \
		sudo xbps-install -Su base-devel pkg-config gtk+3-devel webkit2gtk-devel; \
	else \
		echo "OS not automatically recognized. Please install gcc, pkg-config, gtk3, and webkit2gtk development headers manually."; \
	fi

install: $(TARGET)
	@echo "Installing $(TARGET) to $(BINDIR)..."
	mkdir -p $(BINDIR)
	mv $(TARGET) $(BINDIR)/$(TARGET)
	chmod +x $(BINDIR)/$(TARGET)
	@echo "Installing desktop entry to $(APPDIR)..."
	mkdir -p $(APPDIR)
	mv fkgmd.desktop $(APPDIR)/
	@echo "Installation complete!"

uninstall:
	@echo "Removing $(TARGET) from $(BINDIR)..."
	rm -f $(BINDIR)/$(TARGET)
	@echo "Removing desktop entry from $(APPDIR)..."
	rm -f $(APPDIR)/fkgmd.desktop
	@echo "Uninstallation complete!"

clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET)
# end
