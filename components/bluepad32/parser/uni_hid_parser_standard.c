// SPDX-License-Identifier: Apache-2.0
#include "parser/uni_hid_parser_standard.h"
#include <string.h>
#include "uni_hid_device.h"
#include "hid_usage.h"
#include "uni_log.h"

enum { MAX_REPORTS = 8 };
typedef struct {
    int16_t axes[6];
    uint16_t buttons, id, bytes;
    uint8_t misc, dpad, axes_present;
} report_state_t;
typedef struct {
    report_state_t reports[MAX_REPORTS];
    uint8_t count, button_max;
    bool ids, valid, xbox;
} standard_t;
_Static_assert(sizeof(standard_t) <= HID_DEVICE_MAX_PARSER_DATA, "standard HID state too large");

static standard_t* state(uni_hid_device_t* d) { return (standard_t*)d->parser_data; }

bool uni_hid_parser_standard_configure(uni_hid_device_t* d) {
    standard_t* s = state(d);
    memset(s, 0, sizeof(*s));
    s->xbox = d->controller_type == CONTROLLER_TYPE_XBoxOneController;
    // The vendored iterator does not implement global Push/Pop. Reject these
    // maps explicitly instead of silently interpreting a different layout.
    btstack_hid_descriptor_iterator_t desc;
    btstack_hid_descriptor_iterator_init(&desc, d->hid_descriptor, d->hid_descriptor_len);
    uint16_t id = HID_REPORT_ID_UNDEFINED;
    unsigned bits = 0, count = 0;
    while (btstack_hid_descriptor_iterator_has_more(&desc)) {
        const hid_descriptor_item_t* item = btstack_hid_descriptor_iterator_get_item(&desc);
        if (item->item_type == Global) {
            if (item->item_tag >= Push) return false;
            if (item->item_tag == ReportID) {
                if (item->item_value < 1 || item->item_value > 255) return false;
                // BTstack resets bit offsets on an ID change. Do not accept
                // an interleaved return to an earlier report definition.
                if (id != item->item_value)
                    for (unsigned i = 0; i < s->count; i++)
                        if (s->reports[i].id == item->item_value) return false;
                id = item->item_value; s->ids = true;
            }
            if (item->item_tag == ReportSize) bits = item->item_value;
            if (item->item_tag == ReportCount) count = item->item_value;
        }
        if (item->item_type != Main || item->item_tag != Input) continue;
        if (!bits || bits > 16 || !count || count > 255) return false;
        unsigned i;
        for (i = 0; i < s->count && s->reports[i].id != id; i++) {}
        if (i == s->count) {
            if (s->count == MAX_REPORTS) return false;
            s->reports[s->count++].id = id;
        }
        // Accumulate all Input fields (including padding) in this report.
        if ((unsigned)s->reports[i].bytes + bits * count > 4096) return false;
        s->reports[i].bytes += bits * count;
    }
    if (!btstack_hid_descriptor_iterator_valid(&desc) || !s->count) return false;
    for (unsigned i = 0; i < s->count; i++) {
        if (s->ids && s->reports[i].id == HID_REPORT_ID_UNDEFINED) return false;
        s->reports[i].bytes = (s->reports[i].bytes + 7) / 8 + s->ids;
    }
    btstack_hid_usage_iterator_t it;
    btstack_hid_usage_iterator_init(&it, d->hid_descriptor, d->hid_descriptor_len, HID_REPORT_TYPE_INPUT);
    while (btstack_hid_usage_iterator_has_more(&it)) {
        uint32_t array_max = it.usage_range ? it.usage_maximum & 0xffffu : 0;
        btstack_hid_usage_item_t item;
        btstack_hid_usage_iterator_get_item(&it, &item);
        unsigned maximum = (item.descriptor_item.item_value & 2) ? item.usage : array_max;
        if (item.usage_page == HID_USAGE_PAGE_BUTTON && maximum > s->button_max)
            s->button_max = maximum > 255 ? 255 : maximum;
    }
    s->valid = true;
    return true;
}

void uni_hid_parser_standard_setup(uni_hid_device_t* d) {
    if (!uni_hid_parser_standard_configure(d)) {
        loge("Unsupported standard BLE report map (size, report count or global stack)\n");
        uni_hid_device_disconnect(d);
        uni_hid_device_delete(d);
        return;
    }
    uni_hid_device_set_ready_complete(d);
}

static void button(report_state_t* r, uint16_t usage, uint8_t maximum) {
    // HID button numbers do not define physical labels. These are the common
    // compact 8/12-button and Android 15/16-button conventions, not universal.
    static const uint16_t compact[] = {BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y,
        BUTTON_SHOULDER_L, BUTTON_SHOULDER_R, BUTTON_TRIGGER_L, BUTTON_TRIGGER_R};
    static const uint16_t android[] = {BUTTON_A, BUTTON_B, 0, BUTTON_X, BUTTON_Y, 0,
        BUTTON_SHOULDER_L, BUTTON_SHOULDER_R, BUTTON_TRIGGER_L, BUTTON_TRIGGER_R};
    if (maximum <= 8) {
        if (usage >= 1 && usage <= 6) r->buttons |= compact[usage - 1];
        if (usage == 7) r->misc |= MISC_BUTTON_SELECT;
        if (usage == 8) r->misc |= MISC_BUTTON_START;
    } else if (maximum <= 12) {
        if (usage >= 1 && usage <= 8) r->buttons |= compact[usage - 1];
        if (usage == 9) r->misc |= MISC_BUTTON_SELECT;
        if (usage == 10) r->misc |= MISC_BUTTON_START;
        if (usage == 11) r->buttons |= BUTTON_THUMB_L;
        if (usage == 12) r->buttons |= BUTTON_THUMB_R;
    } else {
        if (usage >= 1 && usage <= 10) r->buttons |= android[usage - 1];
        if (usage == 11) r->misc |= MISC_BUTTON_SELECT;
        if (usage == 12) r->misc |= MISC_BUTTON_START;
        if (usage == 13) r->misc |= MISC_BUTTON_SYSTEM;
        if (usage == 14) r->buttons |= BUTTON_THUMB_L;
        if (usage == 15) r->buttons |= BUTTON_THUMB_R;
    }
}

void uni_hid_parser_standard_parse(uni_hid_device_t* d, const uint8_t* report, uint16_t len) {
    standard_t* s = state(d);
    if (!s->valid || !report || !len) return;
    uint16_t id = s->ids ? report[0] : HID_REPORT_ID_UNDEFINED;
    unsigned slot;
    for (slot = 0; slot < s->count && s->reports[slot].id != id; slot++) {}
    if (slot == s->count || len < s->reports[slot].bytes) {
        loge("Ignored unknown or truncated BLE input report\n");
        return;
    }
    report_state_t next = {.id = id, .bytes = s->reports[slot].bytes};
    btstack_hid_parser_t p;
    btstack_hid_parser_init(&p, d->hid_descriptor, d->hid_descriptor_len, HID_REPORT_TYPE_INPUT, report, len);
    while (btstack_hid_parser_has_more(&p)) {
        hid_globals_t g = {.logical_minimum = p.usage_iterator.global_logical_minimum,
            .logical_maximum = p.usage_iterator.global_logical_maximum,
            .report_size = p.descriptor_usage_item.size};
        uint16_t page, usage; int32_t value;
        btstack_hid_parser_get_field(&p, &page, &usage, &value);
        if (g.logical_minimum >= 0 && g.logical_maximum == -1)
            g.logical_maximum = (1u << g.report_size) - 1;
        if (page == HID_USAGE_PAGE_BUTTON && value) button(&next, usage, s->button_max);
        if (page == HID_USAGE_PAGE_CONSUMER && value) {
            if (usage == HID_USAGE_AC_BACK) next.misc |= MISC_BUTTON_SELECT;
            if (usage == HID_USAGE_AC_HOME) next.misc |= MISC_BUTTON_START;
        }
        if (page == HID_USAGE_PAGE_SIMULATION_CONTROLS && g.logical_maximum > g.logical_minimum) {
            int pedal = usage == HID_USAGE_BRAKE ? 4 : usage == HID_USAGE_ACCELERATOR ? 5 : -1;
            if (pedal >= 0) {
                next.axes[pedal] = uni_hid_parser_process_pedal(&g, value);
                next.axes_present |= 1u << pedal;
            }
        }
        if (page != HID_USAGE_PAGE_GENERIC_DESKTOP) continue;
        if (s->xbox && (usage == HID_USAGE_AXIS_RX || usage == HID_USAGE_AXIS_RY) &&
            g.logical_maximum > g.logical_minimum) {
            unsigned pedal = usage == HID_USAGE_AXIS_RX ? 4 : 5;
            next.axes[pedal] = uni_hid_parser_process_pedal(&g, value);
            next.axes_present |= 1u << pedal;
            continue;
        }
        if (usage == HID_USAGE_HAT) next.dpad |= uni_hid_parser_hat_to_dpad(uni_hid_parser_process_hat(&g, value));
        if (usage >= HID_USAGE_DPAD_UP && usage <= HID_USAGE_DPAD_LEFT)
            uni_hid_parser_process_dpad(usage, value, &next.dpad);
        int axis = usage == HID_USAGE_AXIS_X ? 0 : usage == HID_USAGE_AXIS_Y ? 1 :
                   (usage == HID_USAGE_AXIS_Z || usage == HID_USAGE_AXIS_RX) ? 2 :
                   (usage == HID_USAGE_AXIS_RZ || usage == HID_USAGE_AXIS_RY) ? 3 : -1;
        if (axis >= 0 && g.logical_maximum > g.logical_minimum) {
            next.axes[axis] = uni_hid_parser_process_axis(&g, value);
            next.axes_present |= 1u << axis;
        }
    }
    s->reports[slot] = next;
    memset(&d->controller, 0, sizeof(d->controller));
    d->controller.klass = UNI_CONTROLLER_CLASS_GAMEPAD;
    uni_gamepad_t* gp = &d->controller.gamepad;
    int32_t* axes[] = {&gp->axis_x, &gp->axis_y, &gp->axis_rx, &gp->axis_ry, &gp->brake, &gp->throttle};
    for (unsigned i = 0; i < s->count; i++) {
        report_state_t* r = &s->reports[i];
        gp->buttons |= r->buttons; gp->misc_buttons |= r->misc; gp->dpad |= r->dpad;
        for (unsigned a = 0; a < 6; a++) if (r->axes_present & (1u << a)) *axes[a] = r->axes[a];
    }
}
