# Builds toi-input, the agent-control input injector (Wayland virtual
# pointer/keyboard). Protocol glue is generated from the vendored XMLs.
BUILD ?= build
PROTOCOLS := protocols
WAYLAND_SCANNER ?= wayland-scanner
CFLAGS ?= -O2 -Wall -Wextra
PKGS := wayland-client xkbcommon

GEN_H := $(BUILD)/wlr-virtual-pointer-unstable-v1-client-protocol.h \
         $(BUILD)/virtual-keyboard-unstable-v1-client-protocol.h
GEN_C := $(BUILD)/wlr-virtual-pointer-unstable-v1-protocol.c \
         $(BUILD)/virtual-keyboard-unstable-v1-protocol.c

$(BUILD)/toi-input: toi_input.c $(GEN_H) $(GEN_C)
	cc $(CFLAGS) -D_GNU_SOURCE -I$(BUILD) -o $@ toi_input.c $(GEN_C) \
	  $(shell pkg-config --cflags --libs $(PKGS))

$(BUILD)/%-client-protocol.h: $(PROTOCOLS)/%.xml | $(BUILD)
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD)/%-protocol.c: $(PROTOCOLS)/%.xml | $(BUILD)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: clean
