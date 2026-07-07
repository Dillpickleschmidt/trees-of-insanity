// Human-input injection for the agent-control seam: synthesizes mouse and
// keyboard events at the Wayland compositor level (wlr virtual-pointer and
// virtual-keyboard protocols), so the running desktop shell receives them
// exactly like real user input. Coordinates are SCREENSHOT pixels (grim
// output); they are normalized against the output so compositor scale never
// enters the interface.
//
//   toi-input move <x> <y>
//   toi-input click <x> <y> [left|right|middle]
//   toi-input down <x> <y> [button] | up [button]
//   toi-input drag <x1> <y1> <x2> <y2> [button]
//   toi-input scroll <x> <y> <steps>          (positive = down)
//   toi-input key <Name> [Name...]            (Enter, Escape, Tab, Left, ...)
//   toi-input type <text>                     (keymap-aware, one keystroke per char)

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    struct wl_display* display;
    struct wl_seat* seat;
    struct wl_output* output;
    int32_t output_width;
    int32_t output_height;
    struct zwlr_virtual_pointer_manager_v1* pointer_manager;
    struct zwp_virtual_keyboard_manager_v1* keyboard_manager;
    struct zwlr_virtual_pointer_v1* pointer;
    struct zwp_virtual_keyboard_v1* keyboard;
    struct xkb_keymap* keymap;
} Agent;

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void pause_ms(long ms)
{
    usleep((useconds_t)(ms * 1000));
}

static void output_mode(void* data, struct wl_output* output, uint32_t flags, int32_t width, int32_t height,
                        int32_t refresh)
{
    (void)output;
    (void)refresh;
    Agent* agent = data;
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
        agent->output_width = width;
        agent->output_height = height;
    }
}

static void output_ignore_geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                                   int32_t sp, const char* mk, const char* md, int32_t tr)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)mk; (void)md; (void)tr;
}
static void output_ignore_done(void* d, struct wl_output* o) { (void)d; (void)o; }
static void output_ignore_scale(void* d, struct wl_output* o, int32_t f) { (void)d; (void)o; (void)f; }
static void output_ignore_name(void* d, struct wl_output* o, const char* n) { (void)d; (void)o; (void)n; }
static void output_ignore_description(void* d, struct wl_output* o, const char* n) { (void)d; (void)o; (void)n; }

static const struct wl_output_listener kOutputListener = {
    .geometry = output_ignore_geometry,
    .mode = output_mode,
    .done = output_ignore_done,
    .scale = output_ignore_scale,
    .name = output_ignore_name,
    .description = output_ignore_description,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface,
                            uint32_t version)
{
    Agent* agent = data;
    if (strcmp(interface, wl_seat_interface.name) == 0 && agent->seat == NULL) {
        agent->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
    } else if (strcmp(interface, wl_output_interface.name) == 0 && agent->output == NULL) {
        agent->output = wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
        wl_output_add_listener(agent->output, &kOutputListener, agent);
    } else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        agent->pointer_manager =
            wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, version < 2 ? version : 2);
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        agent->keyboard_manager =
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener kRegistryListener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int connect_agent(Agent* agent)
{
    agent->display = wl_display_connect(NULL);
    if (agent->display == NULL) {
        fprintf(stderr, "toi-input: no Wayland display\n");
        return 0;
    }
    struct wl_registry* registry = wl_display_get_registry(agent->display);
    wl_registry_add_listener(registry, &kRegistryListener, agent);
    wl_display_roundtrip(agent->display); // globals
    wl_display_roundtrip(agent->display); // output mode
    if (agent->seat == NULL || agent->output == NULL || agent->output_width <= 0) {
        fprintf(stderr, "toi-input: missing seat/output\n");
        return 0;
    }
    return 1;
}

static int ensure_pointer(Agent* agent)
{
    if (agent->pointer != NULL) {
        return 1;
    }
    if (agent->pointer_manager == NULL) {
        fprintf(stderr, "toi-input: compositor lacks zwlr_virtual_pointer_manager_v1\n");
        return 0;
    }
    agent->pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(
        agent->pointer_manager, agent->seat, agent->output);
    return agent->pointer != NULL;
}

// Screenshot pixels map 1:1 onto the output mode; motion_absolute normalizes
// x/extent, so compositor scaling cancels out.
static void pointer_move(Agent* agent, long x, long y)
{
    zwlr_virtual_pointer_v1_motion_absolute(agent->pointer, now_ms(), (uint32_t)x, (uint32_t)y,
                                            (uint32_t)agent->output_width, (uint32_t)agent->output_height);
    zwlr_virtual_pointer_v1_frame(agent->pointer);
    wl_display_flush(agent->display);
    pause_ms(25);
}

static void pointer_button(Agent* agent, uint32_t button, int pressed)
{
    zwlr_virtual_pointer_v1_button(agent->pointer, now_ms(), button,
                                   pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(agent->pointer);
    wl_display_flush(agent->display);
    pause_ms(40);
}

static void pointer_scroll(Agent* agent, long steps)
{
    const long count = steps < 0 ? -steps : steps;
    const double direction = steps < 0 ? -15.0 : 15.0;
    for (long i = 0; i < count; ++i) {
        zwlr_virtual_pointer_v1_axis_source(agent->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
        zwlr_virtual_pointer_v1_axis_discrete(agent->pointer, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL,
                                              wl_fixed_from_double(direction), steps < 0 ? -1 : 1);
        zwlr_virtual_pointer_v1_frame(agent->pointer);
        wl_display_flush(agent->display);
        pause_ms(30);
    }
}

static uint32_t button_from_name(const char* name)
{
    if (name == NULL || strcmp(name, "left") == 0) return BTN_LEFT;
    if (strcmp(name, "right") == 0) return BTN_RIGHT;
    if (strcmp(name, "middle") == 0) return BTN_MIDDLE;
    fprintf(stderr, "toi-input: unknown button '%s'\n", name);
    exit(2);
}

// --- keyboard ---------------------------------------------------------------

static int ensure_keyboard(Agent* agent)
{
    if (agent->keyboard != NULL) {
        return 1;
    }
    if (agent->keyboard_manager == NULL) {
        fprintf(stderr, "toi-input: compositor lacks zwp_virtual_keyboard_manager_v1\n");
        return 0;
    }
    agent->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(agent->keyboard_manager, agent->seat);

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    agent->keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(context);
    if (agent->keymap == NULL) {
        fprintf(stderr, "toi-input: failed to build keymap\n");
        return 0;
    }
    char* keymap_string = xkb_keymap_get_as_string(agent->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    const size_t size = strlen(keymap_string) + 1;
    const int fd = memfd_create("toi-input-keymap", 0);
    if (fd < 0 || write(fd, keymap_string, size) != (ssize_t)size) {
        fprintf(stderr, "toi-input: failed to share keymap\n");
        return 0;
    }
    free(keymap_string);
    zwp_virtual_keyboard_v1_keymap(agent->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
    wl_display_flush(agent->display);
    pause_ms(20);
    return 1;
}

static void key_event(Agent* agent, uint32_t evdev_code, int pressed)
{
    zwp_virtual_keyboard_v1_key(agent->keyboard, now_ms(), evdev_code,
                                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_flush(agent->display);
    pause_ms(15);
}

static void key_tap(Agent* agent, uint32_t evdev_code, int shifted)
{
    if (shifted) {
        key_event(agent, KEY_LEFTSHIFT, 1);
    }
    key_event(agent, evdev_code, 1);
    key_event(agent, evdev_code, 0);
    if (shifted) {
        key_event(agent, KEY_LEFTSHIFT, 0);
    }
}

// Find which keycode/level produces the keysym under the active keymap.
static int find_key_for_keysym(struct xkb_keymap* keymap, xkb_keysym_t keysym, uint32_t* out_evdev, int* out_shifted)
{
    const xkb_keycode_t min = xkb_keymap_min_keycode(keymap);
    const xkb_keycode_t max = xkb_keymap_max_keycode(keymap);
    for (xkb_keycode_t keycode = min; keycode <= max; ++keycode) {
        for (xkb_level_index_t level = 0; level < 2; ++level) {
            const xkb_keysym_t* syms = NULL;
            const int count = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, level, &syms);
            for (int index = 0; index < count; ++index) {
                if (syms[index] == keysym) {
                    *out_evdev = keycode - 8; // xkb keycodes are evdev + 8
                    *out_shifted = level == 1;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static const struct {
    const char* name;
    uint32_t code;
} kNamedKeys[] = {
    {"Enter", KEY_ENTER},   {"Escape", KEY_ESC},      {"Tab", KEY_TAB},       {"Backspace", KEY_BACKSPACE},
    {"Delete", KEY_DELETE}, {"Space", KEY_SPACE},     {"Left", KEY_LEFT},     {"Right", KEY_RIGHT},
    {"Up", KEY_UP},         {"Down", KEY_DOWN},       {"Home", KEY_HOME},     {"End", KEY_END},
    {"PageUp", KEY_PAGEUP}, {"PageDown", KEY_PAGEDOWN},
};

static int type_text(Agent* agent, const char* text)
{
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const unsigned char ch = (unsigned char)*cursor;
        if (ch > 0x7f) {
            fprintf(stderr, "toi-input: type only supports ASCII (got 0x%02x)\n", ch);
            return 0;
        }
        const xkb_keysym_t keysym = ch == '\n' ? XKB_KEY_Return : xkb_utf32_to_keysym((uint32_t)ch);
        uint32_t evdev = 0;
        int shifted = 0;
        if (keysym == XKB_KEY_NoSymbol || !find_key_for_keysym(agent->keymap, keysym, &evdev, &shifted)) {
            fprintf(stderr, "toi-input: no key produces '%c' in the active layout\n", ch);
            return 0;
        }
        key_tap(agent, evdev, shifted);
    }
    return 1;
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: toi-input move|click|down|up|drag|scroll|key|type ... (screenshot-pixel coordinates)\n");
        return 2;
    }
    Agent agent = {0};
    if (!connect_agent(&agent)) {
        return 1;
    }
    const char* command = argv[1];

    if (strcmp(command, "move") == 0 && argc >= 4) {
        if (!ensure_pointer(&agent)) return 1;
        pointer_move(&agent, atol(argv[2]), atol(argv[3]));
    } else if (strcmp(command, "click") == 0 && argc >= 4) {
        if (!ensure_pointer(&agent)) return 1;
        const uint32_t button = button_from_name(argc > 4 ? argv[4] : NULL);
        pointer_move(&agent, atol(argv[2]), atol(argv[3]));
        pointer_button(&agent, button, 1);
        pointer_button(&agent, button, 0);
    } else if (strcmp(command, "down") == 0 && argc >= 4) {
        if (!ensure_pointer(&agent)) return 1;
        pointer_move(&agent, atol(argv[2]), atol(argv[3]));
        pointer_button(&agent, button_from_name(argc > 4 ? argv[4] : NULL), 1);
    } else if (strcmp(command, "up") == 0) {
        if (!ensure_pointer(&agent)) return 1;
        pointer_button(&agent, button_from_name(argc > 2 ? argv[2] : NULL), 0);
    } else if (strcmp(command, "drag") == 0 && argc >= 6) {
        if (!ensure_pointer(&agent)) return 1;
        const long x1 = atol(argv[2]), y1 = atol(argv[3]), x2 = atol(argv[4]), y2 = atol(argv[5]);
        const uint32_t button = button_from_name(argc > 6 ? argv[6] : NULL);
        pointer_move(&agent, x1, y1);
        pointer_button(&agent, button, 1);
        const int steps = 24; // human-like interpolated motion
        for (int step = 1; step <= steps; ++step) {
            pointer_move(&agent, x1 + (x2 - x1) * step / steps, y1 + (y2 - y1) * step / steps);
        }
        pointer_button(&agent, button, 0);
    } else if (strcmp(command, "scroll") == 0 && argc >= 5) {
        if (!ensure_pointer(&agent)) return 1;
        pointer_move(&agent, atol(argv[2]), atol(argv[3]));
        pointer_scroll(&agent, atol(argv[4]));
    } else if (strcmp(command, "key") == 0 && argc >= 3) {
        if (!ensure_keyboard(&agent)) return 1;
        for (int index = 2; index < argc; ++index) {
            uint32_t code = 0;
            for (size_t entry = 0; entry < sizeof(kNamedKeys) / sizeof(kNamedKeys[0]); ++entry) {
                if (strcmp(kNamedKeys[entry].name, argv[index]) == 0) {
                    code = kNamedKeys[entry].code;
                    break;
                }
            }
            if (code == 0) {
                fprintf(stderr, "toi-input: unknown key '%s'\n", argv[index]);
                return 2;
            }
            key_tap(&agent, code, 0);
        }
    } else if (strcmp(command, "type") == 0 && argc >= 3) {
        if (!ensure_keyboard(&agent)) return 1;
        if (!type_text(&agent, argv[2])) {
            return 1;
        }
    } else {
        fprintf(stderr, "toi-input: bad arguments for '%s'\n", command);
        return 2;
    }

    wl_display_roundtrip(agent.display);
    return 0;
}
