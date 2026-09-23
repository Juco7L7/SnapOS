CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS  ?= -lm
PREFIX  ?= /usr/local
BIN      = bin
PKG_CONFIG ?= pkg-config
GUICFLAGS ?= -std=gnu11 -O2 -Wall

TOOLS    = snapos snapctl snappy snapguard snapguard-watch snap-deb snapconfig apt
BINS     = $(addprefix $(BIN)/,$(TOOLS))

all: $(BINS)

gui: $(BIN)/snapguard-gui $(BIN)/snaphelper $(BIN)/snapupdate

$(BIN)/snapguard-gui: src/snapguard-gui.c | $(BIN)
	$(CC) $(GUICFLAGS) -o $@ $< $$($(PKG_CONFIG) --cflags gtk+-3.0) $(LDFLAGS) $$($(PKG_CONFIG) --libs gtk+-3.0) -lm

$(BIN)/snaphelper: src/snaphelper.c | $(BIN)
	$(CC) $(GUICFLAGS) -o $@ $< $$($(PKG_CONFIG) --cflags gtk+-3.0) $(LDFLAGS) $$($(PKG_CONFIG) --libs gtk+-3.0)

$(BIN)/snapupdate: src/snapupdate.c | $(BIN)
	$(CC) $(GUICFLAGS) -o $@ $< $$($(PKG_CONFIG) --cflags gtk+-3.0) $(LDFLAGS) $$($(PKG_CONFIG) --libs gtk+-3.0)

$(BIN)/%: src/%.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BIN):
	mkdir -p $(BIN)

test: all
	bash tests/run.sh

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 $(BINS) $(DESTDIR)$(PREFIX)/bin/
	install -Dm644 branding/snap-deb.desktop $(DESTDIR)$(PREFIX)/share/applications/snap-deb.desktop

clean:
	rm -rf $(BIN)

.PHONY: all gui test install clean
