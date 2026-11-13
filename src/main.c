#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#include <windows.h>
#include <bcrypt.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "mac128k.h"
#include "mac_live_window.h"
#include "mac_mouse_api.h"
#include "mac_mouse_trace.h"
#include "pc286.h"
#include "nes.h"
#include "x64_media.h"
#include "x64_soft_probe.h"
#include "video_capture.h"
#include "media_convert.h"

enum {
    MAX_INSPECT_BYTES = 4096,
    MAX_INSPECT_RANGES = 16,
    MAX_INSPECT_TOTAL = 16384,
    MAX_INPUT_EVENTS = 1024,
    MAX_TIMEOUT_MS = 3600000,
    CYCLE_SLICE = 256,
    MAC_CLOCK_HZ = 7833600,
    LIVE_PUMP_INTERVAL_MS = 2,
};
#define MAX_CYCLES UINT32_C(4000000000)

struct predicate {
    uint32_t address;
    uint32_t width;
    uint32_t value;
};

struct inspection {
    uint32_t address;
    uint32_t length;
};

enum input_kind { INPUT_BUTTON, INPUT_PHASE, INPUT_MOTION, INPUT_MAC_KEY, INPUT_PC_SCAN, INPUT_WINDOW };

struct input_event {
    uint32_t cycle;
    enum input_kind kind;
    int32_t first;
    int32_t second;
    uint32_t action;
};

enum machine_kind { MACHINE_UNSET, MACHINE_MAC128K, MACHINE_PC286 };

struct request {
    enum machine_kind machine;
    const wchar_t *rom_path;
    const wchar_t *floppy_path;
    const wchar_t *disk_path;
    const wchar_t *disk_out_path;
    const wchar_t *ega_rom_path;
    const wchar_t *ega_font_path;
    const wchar_t *frame_path;
    const wchar_t *video_path;
    const wchar_t *cancel_event;
    const wchar_t *mouse_record_path;
    const wchar_t *mouse_replay_path;
    const wchar_t *mouse_witness_path;
    const wchar_t *pc_witness_path;
    uint32_t cycle_limit;
    uint32_t timeout_ms;
    uint32_t video_fps, video_limit, video_start;
    bool video_fps_set, video_limit_set, video_start_set;
    struct predicate until;
    struct predicate input_after;
    struct inspection pc_witness_memory, pc_witness_io;
    struct inspection mouse_witness_memory;
    uint32_t pc_witness_first, pc_witness_last;
    bool pc_witness_cycles_set;
    bool fail_floppy_read_set;
    uint32_t fail_floppy_read[3];
    struct inspection inspect[MAX_INSPECT_RANGES];
    size_t inspect_count;
    uint32_t inspect_total;
    bool stop_at_pc;
    uint32_t target_pc;
    bool stop_at_post;
    uint32_t target_post;
    uint32_t live_scale;
    uint32_t mouse_edge_cycles;
    uint32_t mouse_vbl_guard;
    uint32_t mouse_witness_first;
    uint32_t mouse_witness_last;
    uint8_t mouse_dcd_invert;
    bool mouse_invert_set;
    bool mouse_guard_set;
    bool mouse_witness_range_set;
    bool mouse_witness_detail;
    bool raw_mouse_phases;
    bool relative_motion;
    bool window_input;
    struct input_event input_events[MAX_INPUT_EVENTS];
    size_t input_event_count;
};

struct image {
    uint8_t *bytes;
    DWORD size;
};

struct machine {
    mac128k *mac;
    pc286 *pc;
};

struct mouse_session {
    mac128k *mac;
    mac_mouse *bridge;
    mac_live_window *window;
    mac_mouse_trace *record;
    mac_mouse_trace *replay;
    size_t replay_index;
    uint64_t accepted;
    uint64_t emitted;
    bool injected_window;
    bool readiness_requested;
    bool ready;
    uint64_t ready_cycle;
    size_t scheduled;
    size_t scheduled_accepted;
    uint8_t source;
    uint32_t edge_cycles, guard_cycles;
    uint8_t invert;
    uint64_t last_guard_due, last_guard_deadline;
    bool guard_noted;
    bool witness_detail;
};

struct machine_state {
    uint64_t cycles;
    uint64_t vbl;
    uint32_t pc;
    uint32_t fault_address;
    const char *fault_operation;
    uint16_t cs;
    uint16_t ip;
    int last_exception;
    uint8_t post;
    bool post_written;
    bool halted;
    bool read_failure_enabled;
    uint8_t read_failure_cylinder, read_failure_head, read_failure_sector;
    uint64_t read_failures;
};

static volatile LONG cancel_requested;

static BOOL WINAPI on_console_event(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    InterlockedExchange(&cancel_requested, 1);
    return TRUE;
}

static bool parse_number(const wchar_t **position, uint32_t *result)
{
    const wchar_t *cursor = *position;
    uint32_t value = 0;
    uint32_t base = 10;

    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
        base = 16;
    }

    const wchar_t *first_digit = cursor;
    for (;;) {
        uint32_t digit;
        if (*cursor >= '0' && *cursor <= '9') {
            digit = (uint32_t)(*cursor - '0');
        } else if (base == 16 && *cursor >= 'a' && *cursor <= 'f') {
            digit = (uint32_t)(*cursor - 'a') + 10;
        } else if (base == 16 && *cursor >= 'A' && *cursor <= 'F') {
            digit = (uint32_t)(*cursor - 'A') + 10;
        } else {
            break;
        }

        if (value > (UINT32_MAX - digit) / base) {
            return false;
        }
        value = value * base + digit;
        ++cursor;
    }

    if (cursor == first_digit) {
        return false;
    }
    *position = cursor;
    *result = value;
    return true;
}

static bool parse_fields(const wchar_t *text, uint32_t *fields, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (!parse_number(&text, &fields[index])) {
            return false;
        }
        if (index + 1 < count && *text++ != ':') {
            return false;
        }
    }
    return *text == '\0';
}

static bool parse_u64(const wchar_t *text, uint64_t *result)
{
    uint64_t value = 0, base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { text += 2; base = 16; }
    const wchar_t *first = text;
    while (*text) {
        unsigned digit;
        if (*text >= '0' && *text <= '9') digit = (unsigned)(*text - '0');
        else if (base == 16 && *text >= 'a' && *text <= 'f') digit = (unsigned)(*text - 'a') + 10;
        else if (base == 16 && *text >= 'A' && *text <= 'F') digit = (unsigned)(*text - 'A') + 10;
        else return false;
        if (value > (UINT64_MAX - digit) / base) return false;
        value = value * base + digit;
        ++text;
    }
    if (text == first) return false;
    *result = value;
    return true;
}

static bool parse_delta(const wchar_t **text, int32_t *delta)
{
    const bool negative = **text == L'-';
    if (negative) {
        ++*text;
    }
    uint32_t magnitude;
    if (!parse_number(text, &magnitude) || magnitude > (uint32_t)INT32_MAX + negative) {
        return false;
    }
    *delta = negative ? (int32_t)-(int64_t)magnitude : (int32_t)magnitude;
    return true;
}

static bool add_input_event(struct request *request, enum input_kind kind, const wchar_t *value)
{
    const size_t count = request->input_event_count;
    if (count == MAX_INPUT_EVENTS) {
        return false;
    }
    struct input_event event = {.kind = kind};
    if (kind == INPUT_WINDOW) {
        uint32_t fields[4];
        if (!parse_fields(value, fields, 4) || fields[1] >= 512 || fields[2] >= 342 || fields[3] > 3) {
            return false;
        }
        event.cycle = fields[0]; event.first = (int32_t)fields[1];
        event.second = (int32_t)fields[2]; event.action = fields[3];
    } else if (kind == INPUT_MOTION) {
        if (!parse_number(&value, &event.cycle) || *value++ != L':' ||
            !parse_delta(&value, &event.first) || *value++ != L':' ||
            !parse_delta(&value, &event.second) || *value != L'\0') {
            return false;
        }
    } else {
        const bool two_arguments = kind == INPUT_PHASE || kind == INPUT_MAC_KEY;
        uint32_t fields[3] = {0};
        if (!parse_fields(value, fields, two_arguments ? 3 : 2)) {
            return false;
        }
        switch (kind) {
        case INPUT_BUTTON:
            if (fields[1] > 1) {
                return false;
            }
            break;
        case INPUT_PHASE:
            if (fields[1] > 3 || fields[2] > 3) {
                return false;
            }
            break;
        case INPUT_MAC_KEY:
            if (fields[1] > MAC128K_MAX_KEY_CODE || fields[2] > 1) {
                return false;
            }
            break;
        case INPUT_PC_SCAN:
            if (fields[1] > 0xff) {
                return false;
            }
            break;
        case INPUT_MOTION:
        case INPUT_WINDOW:
            return false;
        }
        event.cycle = fields[0];
        event.first = (int32_t)fields[1];
        event.second = (int32_t)fields[2];
    }
    if (count != 0 && event.cycle < request->input_events[count - 1].cycle) {
        return false;
    }
    request->raw_mouse_phases |= kind == INPUT_PHASE;
    request->relative_motion |= kind == INPUT_MOTION;
    request->window_input |= kind == INPUT_WINDOW;
    request->input_events[count] = event;
    ++request->input_event_count;
    return true;
}

static const char *parse_request(int argc, wchar_t **argv, struct request *request)
{
    if (argc % 2 == 0) {
        return "expected pairs of options and values";
    }

    for (int index = 1; index < argc; index += 2) {
        const wchar_t *option = argv[index];
        const wchar_t *value = argv[index + 1];
        uint32_t fields[3];

        if (*value == '\0') {
            return "option values must be nonempty";
        }
        if (wcscmp(option, L"--machine") == 0 && request->machine == MACHINE_UNSET) {
            if (wcscmp(value, L"mac128k") == 0) {
                request->machine = MACHINE_MAC128K;
            } else if (wcscmp(value, L"pc286") == 0) {
                request->machine = MACHINE_PC286;
            } else {
                return "machine must be mac128k or pc286";
            }
        } else if (wcscmp(option, L"--rom") == 0 && request->rom_path == NULL) {
            request->rom_path = value;
        } else if (wcscmp(option, L"--floppy") == 0 && request->floppy_path == NULL) {
            request->floppy_path = value;
        } else if (wcscmp(option, L"--fail-floppy-read") == 0 && !request->fail_floppy_read_set) {
            if (!parse_fields(value, fields, 3) || fields[0] >= 80 || fields[1] >= 2 || fields[2] < 1 || fields[2] > 15)
                return "fail-floppy-read requires cylinder:head:sector within 80:2:15 geometry";
            request->fail_floppy_read_set = true;
            memcpy(request->fail_floppy_read, fields, sizeof(request->fail_floppy_read));
        } else if (wcscmp(option, L"--hard-disk") == 0 && request->disk_path == NULL) {
            request->disk_path = value;
        } else if (wcscmp(option, L"--disk-out") == 0 && request->disk_out_path == NULL) {
            request->disk_out_path = value;
        } else if (wcscmp(option, L"--ega-rom") == 0 && request->ega_rom_path == NULL) {
            request->ega_rom_path = value;
        } else if (wcscmp(option, L"--ega-font") == 0 && request->ega_font_path == NULL) {
            request->ega_font_path = value;
        } else if (wcscmp(option, L"--frame") == 0 && request->frame_path == NULL) {
            request->frame_path = value;
        } else if (wcscmp(option, L"--video") == 0 && request->video_path == NULL) {
            request->video_path = value;
        } else if (wcscmp(option, L"--video-fps") == 0 && !request->video_fps_set) {
            if (!parse_fields(value, fields, 1) || fields[0] < 1 || fields[0] > 60)
                return "video-fps must be between 1 and 60";
            request->video_fps_set = true; request->video_fps = fields[0];
        } else if (wcscmp(option, L"--video-frames") == 0 && !request->video_limit_set) {
            if (!parse_fields(value, fields, 1) || fields[0] < 1 || fields[0] > 3600)
                return "video-frames must be between 1 and 3600";
            request->video_limit_set = true; request->video_limit = fields[0];
        } else if (wcscmp(option, L"--video-start") == 0 && !request->video_start_set) {
            if (!parse_fields(value, fields, 1) || fields[0] > MAX_CYCLES)
                return "video-start must be between 0 and 4000000000";
            request->video_start_set = true; request->video_start = fields[0];
        } else if (wcscmp(option, L"--cancel-event") == 0 && request->cancel_event == NULL) {
            request->cancel_event = value;
        } else if (wcscmp(option, L"--live") == 0 && request->live_scale == 0) {
            if (!parse_fields(value, fields, 1) || fields[0] < 1 || fields[0] > 4) {
                return "live requires a window scale from 1 to 4";
            }
            request->live_scale = fields[0];
        } else if (wcscmp(option, L"--mouse-record") == 0 && request->mouse_record_path == NULL) {
            request->mouse_record_path = value;
        } else if (wcscmp(option, L"--mouse-replay") == 0 && request->mouse_replay_path == NULL) {
            request->mouse_replay_path = value;
        } else if (wcscmp(option, L"--mouse-witness") == 0 && request->mouse_witness_path == NULL) {
            request->mouse_witness_path = value;
        } else if (wcscmp(option, L"--mouse-witness-detail") == 0 && !request->mouse_witness_detail) {
            if (wcscmp(value, L"1") != 0) return "mouse-witness-detail requires 1";
            request->mouse_witness_detail = true;
        } else if (wcscmp(option, L"--mouse-witness-memory") == 0 && !request->mouse_witness_memory.length) {
            if (!parse_fields(value, fields, 2) || fields[1] == 0 || fields[0] >= MAC128K_RAM_SIZE ||
                fields[1] > MAC128K_RAM_SIZE - fields[0])
                return "mouse-witness-memory requires an address:length within Mac RAM";
            request->mouse_witness_memory = (struct inspection){fields[0], fields[1]};
        } else if (wcscmp(option, L"--pc-witness") == 0 && request->pc_witness_path == NULL) {
            request->pc_witness_path = value;
        } else if (wcscmp(option, L"--pc-witness-memory") == 0 && !request->pc_witness_memory.length) {
            if (!parse_fields(value, fields, 2) || fields[1] == 0 || fields[0] >= 0x1000000 || fields[1] > 0x1000000 - fields[0])
                return "pc-witness-memory requires a valid 24-bit address:length";
            request->pc_witness_memory = (struct inspection){fields[0], fields[1]};
        } else if (wcscmp(option, L"--pc-witness-io") == 0 && !request->pc_witness_io.length) {
            if (!parse_fields(value, fields, 2) || fields[1] == 0 || fields[0] >= 0x10000 || fields[1] > 0x10000 - fields[0])
                return "pc-witness-io requires a valid 16-bit port:length";
            request->pc_witness_io = (struct inspection){fields[0], fields[1]};
        } else if (wcscmp(option, L"--pc-witness-cycles") == 0 && !request->pc_witness_cycles_set) {
            if (!parse_fields(value, fields, 2) || fields[0] > fields[1] || fields[1] > MAX_CYCLES)
                return "pc-witness-cycles requires ordered first:last cycles";
            request->pc_witness_cycles_set = true;
            request->pc_witness_first = fields[0]; request->pc_witness_last = fields[1];
        } else if (wcscmp(option, L"--mouse-witness-range") == 0 && !request->mouse_witness_range_set) {
            if (!parse_fields(value, fields, 2) || fields[0] > fields[1] || fields[1] > MAX_CYCLES) {
                return "mouse-witness-range requires ordered first:last cycles from 0 to 4000000000";
            }
            request->mouse_witness_range_set = true;
            request->mouse_witness_first = fields[0];
            request->mouse_witness_last = fields[1];
        } else if (wcscmp(option, L"--mouse-edge-cycles") == 0 && request->mouse_edge_cycles == 0) {
            if (!parse_fields(value, fields, 1) || fields[0] == 0 || fields[0] > 1000000) {
                return "mouse-edge-cycles must be between 1 and 1000000";
            }
            request->mouse_edge_cycles = fields[0];
        } else if (wcscmp(option, L"--mouse-dcd-invert") == 0 && !request->mouse_invert_set) {
            if (!parse_fields(value, fields, 1) || fields[0] > 3) {
                return "mouse-dcd-invert requires a two-bit mask";
            }
            request->mouse_invert_set = true;
            request->mouse_dcd_invert = (uint8_t)fields[0];
        } else if (wcscmp(option, L"--mouse-vbl-guard") == 0 && !request->mouse_guard_set) {
            if (!parse_fields(value, fields, 1) || fields[0] > MAC_MOUSE_MAX_VBL_GUARD_CYCLES) {
                return "mouse-vbl-guard must be between 0 and 8192";
            }
            request->mouse_guard_set = true;
            request->mouse_vbl_guard = fields[0];
        } else if (wcscmp(option, L"--window-mouse") == 0) {
            if (!add_input_event(request, INPUT_WINDOW, value)) {
                return "invalid window-mouse cycle:x:y:action event or input order";
            }
        } else if (wcscmp(option, L"--mouse-button") == 0) {
            if (!add_input_event(request, INPUT_BUTTON, value)) {
                return "invalid mouse-button cycle:state event or input order";
            }
        } else if (wcscmp(option, L"--mouse-phase") == 0) {
            if (!add_input_event(request, INPUT_PHASE, value)) {
                return "invalid mouse-phase cycle:x:y event or input order";
            }
        } else if (wcscmp(option, L"--mouse-motion") == 0) {
            if (!add_input_event(request, INPUT_MOTION, value)) {
                return "invalid mouse-motion cycle:dx:dy event or input order";
            }
        } else if (wcscmp(option, L"--key") == 0) {
            if (!add_input_event(request, INPUT_MAC_KEY, value)) {
                return "invalid key cycle:code:state event or input order";
            }
        } else if (wcscmp(option, L"--scancode") == 0) {
            if (!add_input_event(request, INPUT_PC_SCAN, value)) {
                return "invalid scancode cycle:byte event or input order";
            }
        } else if (wcscmp(option, L"--cycles") == 0 && request->cycle_limit == 0) {
            if (!parse_fields(value, fields, 1) || fields[0] == 0 || fields[0] > MAX_CYCLES) {
                return "cycles must be between 1 and 4000000000";
            }
            request->cycle_limit = fields[0];
        } else if (wcscmp(option, L"--timeout-ms") == 0 && request->timeout_ms == 0) {
            if (!parse_fields(value, fields, 1) || fields[0] == 0 || fields[0] > MAX_TIMEOUT_MS) {
                return "timeout-ms must be between 1 and 3600000";
            }
            request->timeout_ms = fields[0];
        } else if (wcscmp(option, L"--until") == 0 && request->until.width == 0) {
            if (!parse_fields(value, fields, 3) ||
                (fields[1] != 1 && fields[1] != 2 && fields[1] != 4) ||
                (fields[1] < 4 && fields[2] >= (UINT32_C(1) << (fields[1] * 8)))) {
                return "until requires a RAM address, width 1, 2, or 4, and a fitting value";
            }
            request->until = (struct predicate){fields[0], fields[1], fields[2]};
        } else if (wcscmp(option, L"--input-after") == 0 && request->input_after.width == 0) {
            if (!parse_fields(value, fields, 3) ||
                (fields[1] != 1 && fields[1] != 2 && fields[1] != 4) ||
                (fields[1] < 4 && fields[2] >= (UINT32_C(1) << (fields[1] * 8)))) {
                return "input-after requires a RAM address, width 1, 2, or 4, and a fitting value";
            }
            request->input_after = (struct predicate){fields[0], fields[1], fields[2]};
        } else if (wcscmp(option, L"--until-pc") == 0 && !request->stop_at_pc) {
            if (!parse_fields(value, fields, 1) || fields[0] > 0xffffff) {
                return "until-pc requires a 24-bit address";
            }
            request->stop_at_pc = true;
            request->target_pc = fields[0];
        } else if (wcscmp(option, L"--until-post") == 0 && !request->stop_at_post) {
            if (!parse_fields(value, fields, 1) || fields[0] > 0xff) {
                return "until-post requires one byte";
            }
            request->stop_at_post = true;
            request->target_post = fields[0];
        } else if (wcscmp(option, L"--inspect") == 0) {
            if (!parse_fields(value, fields, 2) || fields[1] == 0 ||
                fields[1] > MAX_INSPECT_BYTES || request->inspect_count == MAX_INSPECT_RANGES ||
                fields[1] > MAX_INSPECT_TOTAL - request->inspect_total) {
                return "inspect requires 1 to 4096 bytes, up to 16 ranges and 16384 total bytes";
            }
            request->inspect[request->inspect_count++] = (struct inspection){fields[0], fields[1]};
            request->inspect_total += fields[1];
        } else {
            return "unknown or repeated option";
        }
    }

    if (request->machine == MACHINE_UNSET ||
        request->cycle_limit == 0 || request->timeout_ms == 0) {
        return "machine, cycles, and timeout-ms are required";
    }
    if (!request->video_path && (request->video_fps_set || request->video_limit_set || request->video_start_set))
        return "video settings require video";
    if (!request->video_fps_set) request->video_fps = 10;
    if (!request->video_limit_set) request->video_limit = 300;
    if ((request->until.width != 0) + request->stop_at_pc + request->stop_at_post > 1) {
        return "choose one stop condition";
    }
    if (request->input_event_count != 0 &&
        request->input_events[request->input_event_count - 1].cycle >= request->cycle_limit) {
        return "input events must precede the cycle limit";
    }
    const bool mouse_bridge = request->live_scale || request->relative_motion || request->mouse_replay_path || request->window_input;
    const bool mouse_settings = request->mouse_edge_cycles || request->mouse_invert_set ||
                                request->mouse_guard_set;
    if (request->machine != MACHINE_MAC128K &&
        (mouse_bridge || request->mouse_record_path || mouse_settings)) {
        return "live window, mouse recording, replay and motion settings require mac128k";
    }
    if (request->mouse_witness_path != NULL && request->machine != MACHINE_MAC128K) {
        return "mouse-witness requires mac128k";
    }
    if (request->pc_witness_path != NULL && request->machine != MACHINE_PC286) return "pc-witness requires pc286";
    if (request->fail_floppy_read_set && (request->machine != MACHINE_PC286 || !request->floppy_path || request->rom_path))
        return "fail-floppy-read requires pc286, floppy, and built-in firmware";
    if (!request->pc_witness_path && (request->pc_witness_memory.length || request->pc_witness_io.length || request->pc_witness_cycles_set))
        return "pc-witness filters require pc-witness";
    if (!request->pc_witness_cycles_set) request->pc_witness_last = request->cycle_limit;
    if (request->mouse_witness_range_set && request->mouse_witness_path == NULL) {
        return "mouse-witness-range requires mouse-witness";
    }
    if (request->mouse_witness_detail && request->mouse_witness_path == NULL)
        return "mouse-witness-detail requires mouse-witness";
    if (request->mouse_witness_memory.length && (!request->mouse_witness_path || !request->mouse_witness_detail))
        return "mouse-witness-memory requires mouse-witness and mouse-witness-detail 1";
    if (!request->mouse_witness_range_set) {
        request->mouse_witness_last = request->cycle_limit;
    }
    if (request->raw_mouse_phases && mouse_bridge) {
        return "raw mouse phases cannot be mixed with relative motion, replay or live input";
    }
    if (request->mouse_record_path != NULL && request->live_scale == 0 && !request->window_input && !request->relative_motion) {
        return "mouse-record requires live input, relative motion or window-mouse";
    }
    if (request->mouse_replay_path != NULL &&
        (request->live_scale || mouse_settings)) {
        return "mouse-replay supplies its own input events and mouse settings";
    }
    if (!mouse_bridge && mouse_settings) {
        return "mouse settings require live input or relative motion";
    }
    if (request->window_input && (request->live_scale || request->relative_motion || request->mouse_replay_path)) {
        return "window-mouse cannot be mixed with live, relative motion or replay";
    }
    if (request->input_after.width && (!request->input_event_count || request->live_scale || request->mouse_replay_path)) {
        return "input-after requires scheduled input without live input or replay";
    }
    if (request->machine == MACHINE_MAC128K) {
        if (request->rom_path == NULL) {
            return "mac128k requires a ROM";
        }
        if (request->stop_at_post || request->disk_path || request->disk_out_path || request->ega_rom_path || request->ega_font_path) {
            return "POST, hard disk, disk output, and EGA ROM options require pc286";
        }
    } else {
        if (request->rom_path == NULL && request->floppy_path == NULL && request->disk_path == NULL) {
            return "pc286 requires a ROM, floppy, or hard disk";
        }
        if (request->disk_out_path != NULL && request->disk_path == NULL) {
            return "disk-out requires a hard disk";
        }
        if (request->ega_rom_path != NULL && request->rom_path == NULL) {
            return "ega-rom requires a system ROM";
        }
    }
    for (size_t index = 0; index < request->input_event_count; ++index) {
        const enum input_kind kind = request->input_events[index].kind;
        if ((kind == INPUT_PC_SCAN) != (request->machine == MACHINE_PC286)) {
            return "use scancode for pc286 and key or mouse events for mac128k";
        }
        if ((request->live_scale || request->mouse_replay_path) && kind != INPUT_MAC_KEY) {
            return "live input and replay cannot be mixed with scheduled mouse events";
        }
    }
    const uint32_t ram_size = request->machine == MACHINE_MAC128K ? MAC128K_RAM_SIZE : PC286_VIDEO_END;
    if ((request->until.width && request->until.address > ram_size - request->until.width) ||
        (request->input_after.width && request->input_after.address > ram_size - request->input_after.width)) {
        return "requested memory is outside physical RAM and video memory";
    }
    for (size_t index = 0; index < request->inspect_count; ++index) {
        if (request->inspect[index].address > ram_size - request->inspect[index].length) {
            return "requested memory is outside physical RAM and video memory";
        }
    }
    return NULL;
}

static const char *read_image(const wchar_t *path, struct image *image, DWORD minimum_size,
                              DWORD maximum_size, const char *size_error)
{
    if (path == NULL) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return "cannot open guest image for reading";
    }

    LARGE_INTEGER size;
    DWORD bytes_read;
    const char *error = NULL;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileSizeEx(file, &size) ||
        size.QuadPart < minimum_size || size.QuadPart > maximum_size) {
        error = size_error;
    } else {
        image->size = (DWORD)size.QuadPart;
        image->bytes = malloc(image->size);
        if (image->bytes == NULL) {
            error = "cannot allocate guest image";
        } else if (!ReadFile(file, image->bytes, image->size, &bytes_read, NULL) || bytes_read != image->size) {
            error = "cannot read complete guest image";
        }
    }
    CloseHandle(file);
    return error;
}

static bool write_new_file(const wchar_t *path, const char *header, DWORD header_size,
                           const uint8_t *data, DWORD data_size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written;
    bool success = (header_size == 0 ||
                    (WriteFile(file, header, header_size, &written, NULL) && written == header_size)) &&
                   WriteFile(file, data, data_size, &written, NULL) && written == data_size;
    if (!success) {
        FILE_DISPOSITION_INFO disposition = {TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
    }
    if (!CloseHandle(file)) {
        success = false;
    }
    return success;
}

static bool write_mouse_witness(const mac128k *mac, const struct request *request,
                                const struct mouse_session *mouse, uint64_t stop)
{
    static const char fields[] = "cycle kind address reg value x_phase y_phase pressed "
                                 "scc_pending scc_latched scc_dcd via_ifr via_ier irq_level\n";
    static const char *const kinds[] = {
        [MAC128K_WITNESS_PHASE] = "phase",
        [MAC128K_WITNESS_BUTTON] = "button",
        [MAC128K_WITNESS_SCC_READ] = "scc_read",
        [MAC128K_WITNESS_SCC_WRITE] = "scc_write",
        [MAC128K_WITNESS_VIA_READ] = "via_read",
        [MAC128K_WITNESS_VIA_WRITE] = "via_write",
        [MAC128K_WITNESS_IRQ] = "irq",
        [MAC128K_WITNESS_SNAPSHOT] = "snapshot",
        [MAC128K_WITNESS_IRQ_ACCEPT] = "irq_accept",
        [MAC128K_WITNESS_INPUT_DUE] = "input_due",
        [MAC128K_WITNESS_SAMPLE_ACCEPTED] = "sample_accepted",
        [MAC128K_WITNESS_SAMPLE_REJECTED] = "sample_rejected",
        [MAC128K_WITNESS_GUARD] = "guard",
        [MAC128K_WITNESS_RAM_WRITE] = "ram_write",
    };
    size_t count;
    uint64_t dropped;
    const mac128k_mouse_witness_event *events = mac128k_mouse_witness(mac, &count, &dropped);
    enum { MAX_ROW_LENGTH = 256 };
    const size_t capacity = sizeof(fields) + (count + 2) * MAX_ROW_LENGTH;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        return false;
    }
    size_t length;
    if (request->mouse_witness_memory.length) {
        uint64_t matched, ram_dropped;
        mac128k_mouse_witness_memory_stats(mac, &matched, &ram_dropped);
        length = (size_t)snprintf(buffer, capacity,
            "BHMACMOUSE3 %" PRIu32 " %" PRIu32 " %" PRIu64 " %zu %" PRIu64 " %u %u %" PRIu32 " %" PRIu32 " %u %" PRIu32 " %" PRIu32 " %" PRIu64 " %" PRIu64 "\n"
            "seq cycle kind pc address reg value x_phase y_phase pressed scc_pending scc_latched scc_dcd via_ifr via_ier irq_level source dx dy pending due deadline ram_offset width captured_offset captured_width captured_value\n",
            request->mouse_witness_first, request->mouse_witness_last, stop, count, dropped,
            dropped == 0, stop >= request->mouse_witness_last, mouse->edge_cycles, mouse->guard_cycles,
            (unsigned)mouse->invert, request->mouse_witness_memory.address, request->mouse_witness_memory.length,
            matched, ram_dropped);
    } else if (request->mouse_witness_detail) {
        length = (size_t)snprintf(buffer, capacity,
            "BHMACMOUSE2 %" PRIu32 " %" PRIu32 " %" PRIu64 " %zu %" PRIu64 " %u %u %" PRIu32 " %" PRIu32 " %u\n"
            "seq cycle kind pc address reg value x_phase y_phase pressed scc_pending scc_latched scc_dcd via_ifr via_ier irq_level source dx dy pending due deadline\n",
            request->mouse_witness_first, request->mouse_witness_last, stop, count, dropped,
            dropped == 0, stop >= request->mouse_witness_last,
            mouse->edge_cycles, mouse->guard_cycles, (unsigned)mouse->invert);
    } else length = (size_t)snprintf(buffer, capacity,
                                    "BHMACMOUSE1 %" PRIu32 " %" PRIu32 " %" PRIu64 " %zu %" PRIu64 "\n%s",
                                    request->mouse_witness_first, request->mouse_witness_last,
                                    stop, count, dropped, fields);
    for (size_t index = 0; index < count && length < capacity; ++index) {
        const mac128k_mouse_witness_event *event = &events[index];
        if (request->mouse_witness_detail) {
            length += (size_t)snprintf(buffer + length, capacity - length,
                "%zu %" PRIu64 " %s %" PRIu32 " %" PRIu32 " %u %u %u %u %u %u %u %u %u %u %u %u %" PRId32 " %" PRId32 " %" PRIu32 " %" PRIu64 " %" PRIu64,
                index, event->cycle, kinds[event->kind], event->pc, event->address,
                (unsigned)event->reg, (unsigned)event->value, (unsigned)event->x_phase,
                (unsigned)event->y_phase, (unsigned)event->pressed, (unsigned)event->scc_pending,
                (unsigned)event->scc_latched, (unsigned)event->scc_dcd, (unsigned)event->via_ifr,
                (unsigned)event->via_ier, (unsigned)event->irq_level, (unsigned)event->source,
                event->dx, event->dy, event->pending, event->due, event->deadline);
            if (length < capacity && request->mouse_witness_memory.length) {
                length += (size_t)snprintf(buffer + length, capacity - length,
                    " %" PRIu32 " %u %u %u %" PRIu32, event->ram_offset, (unsigned)event->width,
                    (unsigned)event->captured_offset, (unsigned)event->captured_width, event->captured_value);
            }
            if (length < capacity) length += (size_t)snprintf(buffer + length, capacity - length, "\n");
            continue;
        }
        length += (size_t)snprintf(buffer + length, capacity - length,
                                   "%" PRIu64 " %s %" PRIu32 " %u %u %u %u %u %u %u %u %u %u %u\n",
                                   event->cycle, kinds[event->kind], event->address,
                                   (unsigned)event->reg, (unsigned)event->value,
                                   (unsigned)event->x_phase, (unsigned)event->y_phase, (unsigned)event->pressed,
                                   (unsigned)event->scc_pending, (unsigned)event->scc_latched,
                                   (unsigned)event->scc_dcd, (unsigned)event->via_ifr,
                                   (unsigned)event->via_ier, (unsigned)event->irq_level);
    }
    const bool written = length < capacity && write_new_file(request->mouse_witness_path, NULL, 0,
                                                            (const uint8_t *)buffer, (DWORD)length);
    free(buffer);
    return written;
}

static bool write_pc_witness(const pc286 *pc, const struct request *request, uint64_t stop)
{
    static const char *const kinds[] = {"step", "read", "write", "in", "out", "irq", "exception"};
    size_t count;
    uint64_t dropped;
    const pc286_witness_event *events = pc286_witness(pc, &count, &dropped);
    const size_t capacity = (count + 2) * 128;
    char *buffer = malloc(capacity);
    if (!buffer) return false;
    size_t length = (size_t)snprintf(buffer, capacity, "BHPC286W1 %" PRIu64 " %zu %" PRIu64 "\ncycle cs ip pc kind address value width\n", stop, count, dropped);
    for (size_t index = 0; index < count && length < capacity; ++index) {
        const pc286_witness_event *event = &events[index];
        length += (size_t)snprintf(buffer + length, capacity - length,
            "%" PRIu64 " %u %u %" PRIu32 " %s %" PRIu32 " %" PRIu32 " %u\n",
            event->cycle, (unsigned)event->cs, (unsigned)event->ip, event->pc,
            kinds[event->kind], event->address, event->value, (unsigned)event->width);
    }
    const bool written = length < capacity && write_new_file(request->pc_witness_path, NULL, 0, (const uint8_t *)buffer, (DWORD)length);
    free(buffer);
    return written;
}

static const char *open_mouse_session(struct mouse_session *mouse, const struct request *request)
{
    mouse->readiness_requested = request->input_after.width != 0;
    mouse->scheduled = request->input_event_count;
    if (!request->live_scale && !request->relative_motion && !request->mouse_replay_path && !request->window_input) {
        return NULL;
    }
    uint32_t edge_cycles = request->mouse_edge_cycles ? request->mouse_edge_cycles
                                                     : MAC_MOUSE_DEFAULT_EDGE_CYCLES;
    uint32_t vbl_guard = request->mouse_guard_set ? request->mouse_vbl_guard
                                                 : MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES;
    uint8_t invert = request->mouse_dcd_invert;
    if (request->mouse_replay_path != NULL) {
        struct image trace = {0};
        const char *error = read_image(request->mouse_replay_path, &trace, 1,
                                       MAC_MOUSE_TRACE_MAX_BYTES, "invalid mouse trace size");
        if (error == NULL) {
            mouse->replay = mac_mouse_trace_parse(trace.bytes, trace.size);
        }
        free(trace.bytes);
        if (mouse->replay == NULL) {
            return "cannot read a valid BHMOUSE1 mouse trace";
        }
        edge_cycles = mac_mouse_trace_edge_cycles(mouse->replay);
        invert = mac_mouse_trace_dcd_invert(mouse->replay);
        vbl_guard = mac_mouse_trace_vbl_guard_cycles(mouse->replay);
    }
    mouse->edge_cycles = edge_cycles; mouse->guard_cycles = vbl_guard; mouse->invert = invert;
    mouse->bridge = mac_mouse_create_timed(edge_cycles, invert, vbl_guard);
    if (mouse->bridge == NULL) {
        return "cannot initialize the mouse bridge";
    }
    if (request->mouse_record_path != NULL) {
        mouse->record = mac_mouse_trace_create(edge_cycles, invert, vbl_guard);
        if (mouse->record == NULL) {
            return "cannot initialize the mouse trace";
        }
    }
    if (request->live_scale) {
        mouse->window = mac_live_window_create(request->live_scale, true);
        if (mouse->window == NULL) {
            return "cannot create the live Mac window";
        }
    }
    if (request->window_input) {
        mouse->injected_window = true;
        mouse->window = mac_live_window_create(1, false);
        if (mouse->window == NULL) {
            return "cannot create the invisible test window";
        }
    }
    return NULL;
}

static void close_mouse_session(struct mouse_session *mouse)
{
    if (mouse->window != NULL) {
        mac_live_window_destroy(mouse->window);
    }
    mac_mouse_trace_destroy(mouse->record);
    mac_mouse_trace_destroy(mouse->replay);
    mac_mouse_destroy(mouse->bridge);
}

static void note_mouse_guard(struct mouse_session *mouse)
{
    if (!mouse->witness_detail) return;
    uint64_t raw, guarded;
    if (!mac_mouse_unguarded_next_cycle(mouse->bridge, &raw) ||
        !mac_mouse_next_cycle(mouse->bridge, &guarded) || guarded <= raw) {
        mouse->guard_noted = false;
        return;
    }
    if (!mouse->guard_noted || raw != mouse->last_guard_due || guarded != mouse->last_guard_deadline) {
        mac128k_mouse_witness_input(mouse->mac, MAC128K_WITNESS_GUARD, 0, 0, 0, 0,
                                    mac_mouse_pending(mouse->bridge), raw, guarded);
        mouse->last_guard_due = raw; mouse->last_guard_deadline = guarded; mouse->guard_noted = true;
    }
}

static bool queue_mouse_sample(struct mouse_session *mouse, mac_mouse_sample sample)
{
    if (mouse->record != NULL && !mac_mouse_trace_append(mouse->record, sample)) {
        mac128k_mouse_witness_input(mouse->mac, MAC128K_WITNESS_SAMPLE_REJECTED, mouse->source,
            sample.dx, sample.dy, sample.button_changed ? (sample.pressed ? 2 : 1) : 0,
            mac_mouse_pending(mouse->bridge), sample.cycle, 0);
        return false;
    }
    if (!mac_mouse_queue_sample(mouse->bridge, sample.cycle, sample.dx, sample.dy,
                               sample.button_changed, sample.pressed)) {
        if (mouse->record != NULL) {
            mac_mouse_trace_pop(mouse->record);
        }
        mac128k_mouse_witness_input(mouse->mac, MAC128K_WITNESS_SAMPLE_REJECTED, mouse->source,
            sample.dx, sample.dy, sample.button_changed ? (sample.pressed ? 2 : 1) : 0,
            mac_mouse_pending(mouse->bridge), sample.cycle, 0);
        return false;
    }
    ++mouse->accepted;
    mac128k_mouse_witness_pending(mouse->mac, mac_mouse_pending(mouse->bridge));
    mac128k_mouse_witness_input(mouse->mac, MAC128K_WITNESS_SAMPLE_ACCEPTED, mouse->source,
        sample.dx, sample.dy, sample.button_changed ? (sample.pressed ? 2 : 1) : 0,
        mac_mouse_pending(mouse->bridge), sample.cycle, 0);
    note_mouse_guard(mouse);
    return true;
}

static bool read_ram(const struct machine *machine, uint32_t address, uint8_t *bytes, size_t size)
{
    return machine->mac != NULL
        ? mac128k_read_ram(machine->mac, address, bytes, size)
        : pc286_read_ram(machine->pc, address, bytes, size);
}

static struct machine_state get_state(const struct machine *machine)
{
    if (machine->mac != NULL) {
        const mac128k_state state = mac128k_get_state(machine->mac);
        return (struct machine_state){
            .cycles = state.cycles, .vbl = state.vbl, .pc = state.pc,
            .fault_address = state.fault_address, .fault_operation = state.fault_operation};
    }
    const pc286_state state = pc286_get_state(machine->pc);
    return (struct machine_state){
        .cycles = state.cycles, .pc = state.pc, .cs = state.cs, .ip = state.ip,
        .last_exception = state.last_exception,
        .post = state.post, .post_written = state.post_written, .halted = state.halted,
        .read_failure_enabled = state.read_failure_enabled, .read_failure_cylinder = state.read_failure_cylinder,
        .read_failure_head = state.read_failure_head, .read_failure_sector = state.read_failure_sector,
        .read_failures = state.read_failures,
        .fault_address = state.fault_address, .fault_operation = state.fault_operation};
}

static bool matches(const struct machine *machine, struct predicate until)
{
    uint8_t bytes[4];
    if (until.width == 0 || !read_ram(machine, until.address, bytes, until.width)) {
        return false;
    }

    uint32_t actual = 0;
    for (uint32_t index = 0; index < until.width; ++index) {
        const uint32_t byte_index = machine->mac != NULL ? index : until.width - index - 1;
        actual = (actual << 8) | bytes[byte_index];
    }
    return actual == until.value;
}

static void print_result(const char *status, enum machine_kind machine,
                         struct machine_state state, const char *error,
                         const uint8_t *bytes, const struct inspection *inspect, size_t inspect_count,
                         const struct mouse_session *mouse, const video_capture *video)
{
    printf("{\"status\":\"%s\",\"cycles\":%" PRIu64 ",\"pc\":%" PRIu32,
           status, state.cycles, state.pc);
    if (machine == MACHINE_MAC128K) {
        printf(",\"machine\":\"mac128k\",\"vbl\":%" PRIu64, state.vbl);
    } else if (machine == MACHINE_PC286) {
        printf(",\"machine\":\"pc286\",\"cs\":%u,\"ip\":%u,\"halted\":%s",
               (unsigned)state.cs, (unsigned)state.ip, state.halted ? "true" : "false");
        if (state.post_written) {
            printf(",\"post\":%u", (unsigned)state.post);
        }
        if (state.last_exception >= 0) {
            printf(",\"exception\":%d", state.last_exception);
        }
        if (state.read_failure_enabled) {
            printf(",\"floppy_read_failure\":{\"cylinder\":%u,\"head\":%u,\"sector\":%u,\"injected\":%" PRIu64 ",\"bios_status\":32}",
                   state.read_failure_cylinder, state.read_failure_head, state.read_failure_sector, state.read_failures);
        }
    }
    if (error != NULL) {
        printf(",\"error\":\"%s\"", error);
    }
    if (state.fault_operation != NULL) {
        printf(",\"fault\":{\"operation\":\"%s\",\"address\":%" PRIu32 "}",
               state.fault_operation, state.fault_address);
    }
    if (mouse != NULL && mouse->bridge != NULL) {
        printf(",\"mouse\":{\"samples\":%" PRIu64 ",\"steps\":%" PRIu64 ",\"pending\":%" PRIu32 "}",
               mouse->accepted, mouse->emitted, mac_mouse_pending(mouse->bridge));
        if (mouse->injected_window) {
            fputs(",\"input_source\":\"injected_window_messages\"", stdout);
        }
    }
    if (mouse != NULL && mouse->readiness_requested) {
        printf(",\"input_schedule\":{\"ready\":%s", mouse->ready ? "true" : "false");
        if (mouse->ready) { printf(",\"ready_cycle\":%" PRIu64, mouse->ready_cycle); }
        printf(",\"scheduled\":%zu,\"accepted\":%zu}", mouse->scheduled, mouse->scheduled_accepted);
    }
    if (video != NULL) {
        printf(",\"video\":{\"format\":\"y4m\",\"width\":%" PRIu32 ",\"height\":%" PRIu32
               ",\"fps\":%" PRIu32 ",\"start_cycle\":%" PRIu64 ",\"frames\":%" PRIu32
               ",\"frame_limit\":%" PRIu32 ",\"truncated\":%s,\"last_cycle\":%" PRIu64 "}",
               video->width, video->height, video->fps, video->start, video->count, video->limit,
               video->truncated ? "true" : "false", video->last_cycle);
    }
    if (bytes != NULL && inspect_count != 0) {
        fputs(inspect_count == 1 ? ",\"memory\":" : ",\"memory_ranges\":[", stdout);
        size_t offset = 0;
        for (size_t range = 0; range < inspect_count; ++range) {
            if (range != 0) { putchar(','); }
            printf("{\"address\":%" PRIu32 ",\"bytes\":\"", inspect[range].address);
            for (uint32_t index = 0; index < inspect[range].length; ++index) {
                printf("%02x", (unsigned)bytes[offset++]);
            }
            fputs("\"}", stdout);
        }
        if (inspect_count > 1) { putchar(']'); }
    }
    fputs("}\n", stdout);
}

static int request_error(const char *error)
{
    print_result("error", MACHINE_UNSET, (struct machine_state){0}, error, NULL,
                 NULL, 0, NULL, NULL);
    return 2;
}

static bool apply_input(struct machine *machine, struct mouse_session *mouse,
                        struct input_event event, uint64_t cycle)
{
    switch (event.kind) {
    case INPUT_BUTTON:
        if (mouse->bridge != NULL) {
            return queue_mouse_sample(mouse, (mac_mouse_sample){
                .cycle = cycle, .button_changed = true, .pressed = event.first != 0});
        }
        mac128k_set_mouse_button(machine->mac, event.first != 0);
        return true;
    case INPUT_PHASE:
        mac128k_set_mouse_phase(machine->mac, event.first, event.second);
        return true;
    case INPUT_MOTION:
        return queue_mouse_sample(mouse, (mac_mouse_sample){
            .cycle = cycle, .dx = event.first, .dy = event.second});
    case INPUT_WINDOW:
        if (!mac_live_window_inject(mouse->window, event.first, event.second, event.action)) {
            return false;
        }
        mac_live_event input;
        while (mac_live_window_next_event(mouse->window, &input)) {
            if (!queue_mouse_sample(mouse, (mac_mouse_sample){cycle, input.dx, input.dy,
                                                               input.button_changed, input.pressed})) {
                return false;
            }
        }
        return true;
    case INPUT_MAC_KEY:
        return mac128k_inject_key(machine->mac, event.first, event.second != 0);
    case INPUT_PC_SCAN:
        return pc286_inject_scancode(machine->pc, event.first);
    }
    return false;
}

struct run_result {
    const char *status;
    const char *error;
    int exit_code;
};

static mac_live_status refresh_live(struct machine *machine, struct mouse_session *mouse,
                                    struct machine_state state, uint64_t *presented_vbl)
{
    const mac_live_status status = mac_live_window_pump(mouse->window);
    if (status == MAC_LIVE_INPUT_OVERFLOW) {
        return status;
    }
    if (status == MAC_LIVE_RUNNING && state.vbl != *presented_vbl) {
        mac_live_window_present(mouse->window, mac128k_frame(machine->mac));
        *presented_vbl = state.vbl;
    }
    mac_live_event event;
    mouse->source = 4;
    while (mac_live_window_next_event(mouse->window, &event)) {
        if (!queue_mouse_sample(mouse, (mac_mouse_sample){
            state.cycles, event.dx, event.dy, event.button_changed, event.pressed})) {
            return MAC_LIVE_INPUT_OVERFLOW;
        }
    }
    return status;
}

static uint32_t slice_before(uint32_t slice, uint64_t cycle, uint64_t deadline)
{
    const uint64_t distance = deadline - cycle;
    return distance < slice ? (uint32_t)distance : slice;
}

static bool enqueue_replay_samples(struct mouse_session *mouse, uint64_t through_cycle)
{
    if (mouse->replay == NULL) {
        return true;
    }
    const size_t count = mac_mouse_trace_count(mouse->replay);
    const mac_mouse_sample *samples = mac_mouse_trace_samples(mouse->replay);
    mouse->source = 3;
    while (mouse->replay_index < count && samples[mouse->replay_index].cycle <= through_cycle) {
        if (!queue_mouse_sample(mouse, samples[mouse->replay_index])) {
            return false;
        }
        ++mouse->replay_index;
    }
    return true;
}

static struct run_result run_machine(struct machine *machine, const struct request *request,
                                     struct mouse_session *mouse, HANDLE cancel_event, video_capture *video)
{
    LARGE_INTEGER frequency, start;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&start)) {
        return (struct run_result){"error", "cannot start monotonic timer", 2};
    }
    const double timeout_ticks = (double)frequency.QuadPart * request->timeout_ms / 1000.0;
    const LONGLONG pump_interval = frequency.QuadPart * LIVE_PUMP_INTERVAL_MS / 1000;
    const bool use_predicate = request->until.width != 0 || request->stop_at_pc || request->stop_at_post;
    uint64_t cycle_limit = request->cycle_limit;
    const bool reaches_recorded_end = mouse->replay != NULL &&
                                      mac_mouse_trace_stop_cycle(mouse->replay) <= cycle_limit;
    const size_t replay_count = mouse->replay ? mac_mouse_trace_count(mouse->replay) : 0;
    const mac_mouse_sample *replay = mouse->replay ? mac_mouse_trace_samples(mouse->replay) : NULL;
    if (reaches_recorded_end) {
        cycle_limit = mac_mouse_trace_stop_cycle(mouse->replay);
    }
    if (machine->pc != NULL && request->input_event_count != 0) {
        pc286_expect_input(machine->pc, true);
    }
    struct machine_state state = get_state(machine);
    size_t next_event = 0;
    LONGLONG next_pump = start.QuadPart;
    uint64_t presented_vbl = UINT64_MAX;

    for (;;) {
        if (video != NULL && state.cycles >= video_next_cycle(video)) {
            if (video->count == video->limit) {
                video->truncated = true;
            } else {
                const uint8_t *bitmap = machine->mac ? mac128k_frame(machine->mac) : NULL;
                if (machine->pc) pc286_frame_rgb(machine->pc, video->rgb);
                if (!video_append(video, bitmap, video->rgb, state.cycles)) {
                    video->failed = true;
                    return (struct run_result){"error", "cannot write video capture", 2};
                }
            }
        }
        if (state.fault_operation != NULL) {
            return (struct run_result){"machine_fault", NULL, 1};
        }
        const DWORD event_status = cancel_event == NULL
            ? WAIT_TIMEOUT : WaitForSingleObject(cancel_event, 0);
        if (InterlockedCompareExchange(&cancel_requested, 0, 0) != 0 ||
            event_status == WAIT_OBJECT_0) {
            return (struct run_result){"cancelled", NULL, 1};
        }
        if (event_status != WAIT_TIMEOUT) {
            return (struct run_result){"error", "cannot check cancellation event", 2};
        }
        LARGE_INTEGER now;
        if (!QueryPerformanceCounter(&now)) {
            return (struct run_result){"error", "cannot read monotonic timer", 2};
        }
        const LONGLONG elapsed = now.QuadPart - start.QuadPart;
        if ((double)elapsed >= timeout_ticks) {
            return (struct run_result){"timeout", NULL, 1};
        }
        if (mouse->readiness_requested && !mouse->ready && matches(machine, request->input_after)) {
            mouse->ready = true;
            mouse->ready_cycle = state.cycles;
        }
        if ((request->stop_at_pc && state.pc == request->target_pc) ||
            (request->stop_at_post && state.post_written && state.post == request->target_post) ||
            matches(machine, request->until)) {
            return (struct run_result){"condition_met", NULL, 0};
        }
        if (state.halted) {
            return (struct run_result){"halted", NULL, use_predicate ? 1 : 0};
        }
        if (state.cycles >= cycle_limit) {
            // A live pump may accept input at its final boundary without
            // running the guest again. Reproduce its queued, unapplied work.
            if (reaches_recorded_end && !enqueue_replay_samples(mouse, cycle_limit)) {
                return (struct run_result){"input_overflow", "mouse replay queue is full", 1};
            }
            if (mouse->readiness_requested && !mouse->ready) {
                return (struct run_result){"readiness_not_reached", NULL, 1};
            }
            return (struct run_result){"cycle_limit", NULL, use_predicate ? 1 : 0};
        }

        uint64_t live_target = cycle_limit;
        if (request->live_scale != 0) {
            if (now.QuadPart >= next_pump) {
                next_pump = now.QuadPart + pump_interval;
                const mac_live_status status = refresh_live(machine, mouse, state, &presented_vbl);
                if (status == MAC_LIVE_CLOSED) {
                    return (struct run_result){"closed", NULL, use_predicate ? 1 : 0};
                }
                if (status == MAC_LIVE_INPUT_OVERFLOW) {
                    return (struct run_result){"input_overflow", "live mouse input or recording queue is full", 1};
                }
            }
            live_target = (uint64_t)((double)elapsed * MAC_CLOCK_HZ / (double)frequency.QuadPart);
            if (live_target <= state.cycles) {
                const DWORD wait = MsgWaitForMultipleObjectsEx(0, NULL, 1, QS_ALLINPUT, 0);
                if (wait == WAIT_FAILED) {
                    return (struct run_result){"error", "cannot wait for live window input", 2};
                }
                continue;
            }
        }

        while (next_event < request->input_event_count &&
               (!mouse->readiness_requested || mouse->ready) &&
               (uint64_t)request->input_events[next_event].cycle + mouse->ready_cycle <= state.cycles) {
            const struct input_event event = request->input_events[next_event];
            if (machine->mac != NULL && (event.kind == INPUT_BUTTON || event.kind == INPUT_MOTION ||
                                        event.kind == INPUT_PHASE || event.kind == INPUT_WINDOW)) {
                mouse->source = event.kind == INPUT_WINDOW ? 2 : 1;
                mac128k_mouse_witness_input(machine->mac, MAC128K_WITNESS_INPUT_DUE, mouse->source,
                    event.first, event.second, (uint8_t)event.kind,
                    mouse->bridge ? mac_mouse_pending(mouse->bridge) : 0,
                    (uint64_t)event.cycle + mouse->ready_cycle, event.action);
            }
            if (!apply_input(machine, mouse, event, state.cycles)) {
                if (event.kind == INPUT_WINDOW) {
                    return (struct run_result){"input_error", "window mouse event rejected or input queue full", 1};
                }
                if (event.kind == INPUT_MOTION || event.kind == INPUT_BUTTON) {
                    return (struct run_result){"input_overflow", "mouse bridge queue is full", 1};
                }
                return (struct run_result){"machine_fault", "guest keyboard rejected an input event", 1};
            }
            ++next_event;
            mouse->scheduled_accepted = next_event;
            if (machine->pc != NULL && next_event == request->input_event_count) {
                pc286_expect_input(machine->pc, false);
            }
        }

        if (mouse->replay_index < replay_count && !enqueue_replay_samples(mouse, state.cycles)) {
            return (struct run_result){"input_overflow", "mouse replay queue is full", 1};
        }
        uint64_t mouse_deadline = cycle_limit;
        if (mouse->bridge != NULL) {
            mac_mouse_state input;
            uint64_t before_deadline = UINT64_MAX;
            mac_mouse_next_cycle(mouse->bridge, &before_deadline);
            if (mac_mouse_advance(mouse->bridge, state.cycles, &input)) {
                mac128k_mouse_witness_pending(machine->mac, mac_mouse_pending(mouse->bridge));
                mac128k_set_mouse_phase(machine->mac, input.x_phase, input.y_phase);
                mac128k_set_mouse_button(machine->mac, input.pressed);
                ++mouse->emitted;
            } else if (before_deadline <= state.cycles &&
                       mac_mouse_next_cycle(mouse->bridge, &mouse_deadline) && mouse_deadline > state.cycles) {
                mac128k_mouse_witness_input(machine->mac, MAC128K_WITNESS_GUARD, 0, 0, 0, 0,
                    mac_mouse_pending(mouse->bridge), state.cycles, mouse_deadline);
            }
            note_mouse_guard(mouse);
            mac_mouse_next_cycle(mouse->bridge, &mouse_deadline);
        }

        const uint64_t previous_cycles = state.cycles;
        uint32_t slice = use_predicate ? 1 : CYCLE_SLICE;
        slice = slice_before(slice, previous_cycles, cycle_limit);
        slice = slice_before(slice, previous_cycles, live_target);
        slice = slice_before(slice, previous_cycles, mouse_deadline);
        if (video != NULL && video->count < video->limit) {
            const uint64_t video_deadline = video_next_cycle(video);
            if (video_deadline > previous_cycles) slice = slice_before(slice, previous_cycles, video_deadline);
        }
        if (next_event < request->input_event_count && (!mouse->readiness_requested || mouse->ready)) {
            slice = slice_before(slice, previous_cycles, (uint64_t)request->input_events[next_event].cycle + mouse->ready_cycle);
        }
        if (mouse->readiness_requested && !mouse->ready) { slice = 1; }
        if (mouse->replay_index < replay_count) {
            slice = slice_before(slice, previous_cycles, replay[mouse->replay_index].cycle);
        }
        if (machine->mac != NULL) {
            mac128k_run(machine->mac, slice);
        } else {
            pc286_run(machine->pc, slice);
        }
        state = get_state(machine);
        if (state.cycles <= previous_cycles && state.fault_operation == NULL && !state.halted) {
            return (struct run_result){"machine_fault", "CPU did not advance", 1};
        }
    }
}

static const char *create_machine(struct machine *machine, const struct request *request)
{
    const bool mac = request->machine == MACHINE_MAC128K;
    struct image rom = {0}, floppy = {0}, disk = {0}, ega_rom = {0}, ega_font = {0};
    const DWORD rom_size = mac ? MAC128K_ROM_SIZE : PC286_ROM_SIZE;
    const DWORD floppy_size = mac ? MAC128K_FLOPPY_SIZE : PC286_FLOPPY_SIZE;
    const char *error = read_image(request->rom_path, &rom, rom_size, rom_size,
                                   "system ROM must be a 65536-byte file");
    if (error == NULL) {
        error = read_image(request->floppy_path, &floppy, floppy_size,
                           mac ? MAC128K_FLOPPY_SIZE + 84 + 9600 : floppy_size,
                           mac ? "Macintosh floppy must be a 400 KiB raw or Disk Copy 4.2 image"
                               : "PC floppy must be a 1228800-byte raw image");
    }
    if (error == NULL) {
        error = read_image(request->disk_path, &disk, PC286_HARD_DISK_SIZE, PC286_HARD_DISK_SIZE,
                           "hard disk must be a 21411840-byte raw image");
    }
    if (error == NULL) {
        error = read_image(request->ega_rom_path, &ega_rom, 512, PC286_EGA_ROM_MAX,
                           "EGA option ROM must be between 512 and 32768 bytes");
    }
    if (error == NULL) {
        error = read_image(request->ega_font_path, &ega_font, 256 * 14, 256 * 14,
                           "EGA font must contain 3584 bytes");
    }
    if (error == NULL) {
        if (mac) {
            machine->mac = mac128k_create(rom.bytes);
            if (machine->mac != NULL && floppy.bytes != NULL &&
                !mac128k_insert_floppy(machine->mac, floppy.bytes, floppy.size)) {
                error = "invalid Macintosh floppy image or could not insert it";
            }
        } else {
            machine->pc = pc286_create_system(rom.bytes, rom.size, floppy.bytes, floppy.size,
                                              disk.bytes, disk.size);
            if (machine->pc != NULL && ega_rom.bytes != NULL &&
                !pc286_load_ega_rom(machine->pc, ega_rom.bytes, ega_rom.size)) {
                error = "invalid EGA option ROM";
            }
            if (machine->pc != NULL && ega_font.bytes != NULL &&
                !pc286_set_ega_font(machine->pc, ega_font.bytes, ega_font.size)) {
                error = "invalid EGA font";
            }
        }
        if (machine->mac == NULL && machine->pc == NULL) {
            error = "cannot initialize machine or invalid boot image";
        }
    }
    free(ega_font.bytes);
    free(ega_rom.bytes);
    free(disk.bytes);
    free(floppy.bytes);
    free(rom.bytes);
    return error;
}

static void destroy_machine(struct machine *machine)
{
    if (machine->mac != NULL) {
        mac128k_destroy(machine->mac);
    } else {
        pc286_destroy(machine->pc);
    }
}

static bool capture_frame(const struct machine *machine, const wchar_t *path)
{
    if (machine->mac != NULL) {
        static const char header[] = "P4\n512 342\n";
        return write_new_file(path, header, sizeof(header) - 1,
                              mac128k_frame(machine->mac), MAC128K_FRAME_SIZE);
    }
    static const char header[] = "P6\n640 350\n255\n";
    uint8_t *pixels = malloc(PC286_FRAME_SIZE);
    if (pixels == NULL) {
        return false;
    }
    pc286_frame_rgb(machine->pc, pixels);
    const bool success = write_new_file(path, header, sizeof(header) - 1, pixels, PC286_FRAME_SIZE);
    free(pixels);
    return success;
}

static int verify_x64_media(const wchar_t *path, const wchar_t *expected_path)
{
    struct image disk = {0}, expected = {0};
    const char *error = read_image(path, &disk, 67108864, 67108864,
                                   "x64 disk must be a 67108864-byte raw image");
    if (error == NULL && expected_path != NULL) {
        error = read_image(expected_path, &expected, 1, 16 * 1024 * 1024,
                           "expected EFI file must be 1..16777216 bytes");
    }
    x64_media_info info = {0};
    if (error == NULL) error = x64_media_verify(disk.bytes, disk.size, expected.bytes, expected.size, &info);
    free(disk.bytes);
    free(expected.bytes);
    if (error != NULL) return request_error(error);

    printf("{\"status\":\"media_verified\",\"disk_size\":67108864,\"disk_sha256\":\"");
    for (size_t i = 0; i < 32; ++i) printf("%02x", (unsigned)info.image_sha256[i]);
    printf("\",\"esp_lba\":%" PRIu64 ",\"esp_sectors\":%" PRIu32
           ",\"efi_size\":%" PRIu32 ",\"efi_sha256\":\"", info.partition_lba,
           info.partition_sectors, info.efi_size);
    for (size_t i = 0; i < 32; ++i) printf("%02x", (unsigned)info.efi_sha256[i]);
    printf("\",\"entry_rva\":%" PRIu32 ",\"source\":\"gpt_fat32\",\"executed\":false}\n", info.entry_rva);
    return 0;
}

static int map_x64_media(const wchar_t *path, uint64_t base)
{
    struct image disk = {0};
    const char *error = read_image(path, &disk, 67108864, 67108864,
                                   "x64 disk must be a 67108864-byte raw image");
    x64_mapped_media_info info = {0};
    if (!error) error = x64_media_map(disk.bytes, disk.size, base, &info);
    free(disk.bytes);
    if (error) return request_error(error);
    printf("{\"status\":\"image_mapped\",\"source\":\"gpt_fat32\",\"executed\":false,"
           "\"disk_sha256\":\"");
    for (size_t i = 0; i < 32; ++i) printf("%02x", (unsigned)info.media.image_sha256[i]);
    printf("\",\"efi_sha256\":\"");
    for (size_t i = 0; i < 32; ++i) printf("%02x", (unsigned)info.media.efi_sha256[i]);
    printf("\",\"mapped_sha256\":\"");
    for (size_t i = 0; i < 32; ++i) printf("%02x", (unsigned)info.mapped_sha256[i]);
    printf("\",\"preferred_base\":%" PRIu64 ",\"loaded_base\":%" PRIu64
           ",\"entry_address\":%" PRIu64 ",\"image_size\":%" PRIu32 ",\"relocations\":%" PRIu32 "}\n",
           info.preferred_base, info.loaded_base, info.entry_address, info.image_size, info.relocations);
    return 0;
}

static int soft_x64_probe(const wchar_t *path, uint32_t steps)
{
    struct image disk = {0};
    const char *error = read_image(path, &disk, 67108864, 67108864,
                                   "x64 disk must be a 67108864-byte raw image");
    x64_soft_result result = {0}; uint8_t disk_sha[32], efi_sha[32];
    if (!error) error=x64_soft_probe(disk.bytes,disk.size,steps,&result,disk_sha,efi_sha);
    free(disk.bytes);
    if (error) return request_error(error);
    printf("{\"status\":\"%s\",\"backend\":\"experimental_software_probe\",\"firmware\":false,"
           "\"valid_uefi_context\":false,\"steps\":%" PRIu64 ",\"rip\":%" PRIu64
           ",\"rsp\":%" PRIu64 ",\"rax\":%" PRIu64 ",\"rbp\":%" PRIu64
           ",\"r8\":%" PRIu64 ",\"rflags\":%" PRIu64 ",\"opcode\":%u,\"fault_address\":%" PRIu64
           ",\"disk_sha256\":\"",result.stop,result.steps,result.rip,result.rsp,result.rax,
           result.rbp,result.r8,result.rflags,(unsigned)result.opcode,result.fault_address);
    for (size_t i=0;i<32;++i) printf("%02x",(unsigned)disk_sha[i]);
    fputs("\",\"efi_sha256\":\"",stdout);
    for (size_t i=0;i<32;++i) printf("%02x",(unsigned)efi_sha[i]);
    puts("\"}");
    return strcmp(result.stop, "step_limit") == 0 ? 0 : 1;
}

/* NES starts as a separate bounded CPU development target. Unsupported machine
   options must not fall through into Mac or PC behavior. */
static int run_nes(int argc, wchar_t **argv)
{
    const wchar_t *path=NULL,*frame_path=NULL,*audio_path=NULL,*video_path=NULL;
    uint32_t steps=0, timeout=0, target=0, fields[2], ppu_addr=0, ppu_len=0, oam_addr=0, oam_len=0;
    struct {uint32_t address,length;} inspections[8];size_t inspection_count=0;
    bool machine_seen=false, target_set=false, controller_set[2]={false,false}, region_set=false;
    bool video_fps_set=false,video_limit_set=false,video_start_set=false;
    uint32_t video_fps=10,video_limit=300,video_start=0;
    uint32_t controller[2]={0,0}, region=0, last_step[2]={0,0};
    bool have_event[2]={false,false};
    struct nes_event {uint32_t step,mask,port;} events[256];
    size_t event_count=0, next_event=0;
    if (argc%2==0) return request_error("expected pairs of options and values");
    for(int i=1;i<argc;i+=2) {
        const wchar_t *o=argv[i],*v=argv[i+1];
        if(wcscmp(o,L"--machine")==0 && !machine_seen && wcscmp(v,L"nes")==0) machine_seen=true;
        else if(wcscmp(o,L"--rom")==0 && !path && *v) path=v;
        else if(wcscmp(o,L"--frame")==0 && !frame_path && *v) frame_path=v;
        else if(wcscmp(o,L"--audio")==0 && !audio_path && *v) audio_path=v;
        else if(wcscmp(o,L"--video")==0 && !video_path && *v) video_path=v;
        else if(wcscmp(o,L"--video-fps")==0 && !video_fps_set && parse_fields(v,fields,1) && fields[0]>=1 && fields[0]<=60) {video_fps=fields[0];video_fps_set=true;}
        else if(wcscmp(o,L"--video-frames")==0 && !video_limit_set && parse_fields(v,fields,1) && fields[0]>=1 && fields[0]<=3600) {video_limit=fields[0];video_limit_set=true;}
        else if(wcscmp(o,L"--video-start")==0 && !video_start_set && parse_fields(v,fields,1)) {video_start=fields[0];video_start_set=true;}
        else if(wcscmp(o,L"--steps")==0 && !steps && parse_fields(v,fields,1) && fields[0]>=1 && fields[0]<=100000000) steps=fields[0];
        else if(wcscmp(o,L"--timeout-ms")==0 && !timeout && parse_fields(v,fields,1) && fields[0]>=1 && fields[0]<=MAX_TIMEOUT_MS) timeout=fields[0];
        else if(wcscmp(o,L"--until-pc")==0 && !target_set && parse_fields(v,fields,1) && fields[0]<=0xffff) {target=fields[0];target_set=true;}
        else if(wcscmp(o,L"--region")==0 && !region_set && (!wcscmp(v,L"auto") || !wcscmp(v,L"ntsc") || !wcscmp(v,L"pal"))) {region=!wcscmp(v,L"pal")?2:!wcscmp(v,L"ntsc")?1:0;region_set=true;}
        else if((!wcscmp(o,L"--controller1") || !wcscmp(o,L"--controller2")) && parse_fields(v,fields,1) && fields[0]<=255 && !controller_set[!wcscmp(o,L"--controller2")]) {
            unsigned port=!wcscmp(o,L"--controller2");controller[port]=fields[0];controller_set[port]=true;
        }
        else if((!wcscmp(o,L"--controller-event") || !wcscmp(o,L"--controller2-event")) && event_count<256 && parse_fields(v,fields,2) && fields[1]<=255) {
            unsigned port=!wcscmp(o,L"--controller2-event");
            if(have_event[port] && fields[0]<last_step[port]) return request_error("NES controller events must be ordered within each port");
            last_step[port]=fields[0];have_event[port]=true;
            size_t pos=event_count++;
            while(pos && events[pos-1].step>fields[0]) {events[pos]=events[pos-1];--pos;}
            events[pos]=(struct nes_event){fields[0],fields[1],port};
        }
        else if(wcscmp(o,L"--inspect")==0 && inspection_count<8 && parse_fields(v,fields,2) && fields[0]<=0xffff && fields[1]>=1 && fields[1]<=4096 && fields[1]<=0x10000-fields[0]) {inspections[inspection_count].address=fields[0];inspections[inspection_count++].length=fields[1];}
        else if(wcscmp(o,L"--inspect-ppu")==0 && !ppu_len && parse_fields(v,fields,2) && fields[0]<0x4000 && fields[1]>=1 && fields[1]<=4096 && fields[1]<=0x4000-fields[0]) {ppu_addr=fields[0];ppu_len=fields[1];}
        else if(wcscmp(o,L"--inspect-oam")==0 && !oam_len && parse_fields(v,fields,2) && fields[0]<256 && fields[1]>=1 && fields[1]<=256-fields[0]) {oam_addr=fields[0];oam_len=fields[1];}
        else return request_error("invalid, unsupported, or repeated NES option");
    }
    if(!machine_seen || !path || !steps || !timeout) return request_error("nes requires --rom, --steps, and --timeout-ms");
    if(event_count && events[event_count-1].step>=steps) return request_error("NES controller events must precede the step limit");
    if(!video_path && (video_fps_set || video_limit_set || video_start_set)) return request_error("NES video settings require video");
    struct image rom={0};
    const char *error=read_image(path,&rom,16,16+524288+8192,"NES image exceeds supported cartridge dimensions");
    if(error){free(rom.bytes);return request_error(error);}
    uint8_t digest[32]; BCRYPT_ALG_HANDLE alg=NULL;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
       BCryptHash(alg,NULL,0,rom.bytes,rom.size,digest,32)<0) {
        if(alg) BCryptCloseAlgorithmProvider(alg,0);
        free(rom.bytes);
        return request_error("cannot calculate NES cartridge SHA-256");
    }
    BCryptCloseAlgorithmProvider(alg,0);
    nes *n=nes_create_region(rom.bytes,rom.size,region,&error);free(rom.bytes);
    if(!n) return request_error(error);
    nes_set_controller(n,0,(uint8_t)controller[0]);nes_set_controller(n,1,(uint8_t)controller[1]);
    nes_cartridge_info cartridge=nes_get_cartridge_info(n);
    if(audio_path && !nes_capture_audio(n)) {nes_destroy(n);return request_error("cannot allocate NES audio capture");}
    video_capture video={0};
    if(video_path && !video_open_rgb(&video,video_path,256,240,cartridge.pal?1662607:1789773,video_fps,video_limit,video_start)) {
        nes_destroy(n);return request_error("cannot create NES video capture");
    }
    const ULONGLONG started=GetTickCount64(); const char *status="step_limit"; int rc=0;
    for(;;) {
        nes_state s=nes_get_state(n);
        const uint8_t *current_frame=video_path?nes_frame(n):NULL;
        if(current_frame && s.cpu_cycles>=video_next_cycle(&video)) {
            if(video.count==video.limit) video.truncated=true;
            else if(!video_append(&video,NULL,current_frame,s.cpu_cycles)) {video.failed=true;status="error";rc=2;break;}
        }
        if(target_set && s.pc==target) {status="condition_met";break;}
        if(s.steps>=steps) {if(target_set){status="condition_not_met";rc=1;}break;}
        if(GetTickCount64()-started>=timeout) {status="timeout";rc=1;break;}
        while(next_event<event_count && events[next_event].step==s.steps) {
            nes_set_controller(n,events[next_event].port,(uint8_t)events[next_event].mask);++next_event;
        }
        if(!nes_step(n)) {status="machine_fault";rc=1;break;}
    }
    if(video_path && !video_finish(&video,video.failed)) {nes_destroy(n);return request_error("cannot finish NES video capture");}
    if(video.failed) {nes_destroy(n);return request_error("cannot write NES video capture");}
    nes_state state=nes_get_state(n); uint8_t bytes[8][4096],ppu_bytes[4096],oam_bytes[256];
    for(size_t i=0;i<inspection_count;i++) if(!nes_inspect(n,(uint16_t)inspections[i].address,bytes[i],inspections[i].length)) {nes_destroy(n);return request_error("NES inspection must stay within supported RAM or cartridge ROM");}
    if(ppu_len && !nes_inspect_ppu(n,(uint16_t)ppu_addr,ppu_bytes,ppu_len)) {nes_destroy(n);return request_error("invalid NES PPU inspection");}
    if(oam_len && !nes_inspect_oam(n,(uint16_t)oam_addr,oam_bytes,oam_len)) {nes_destroy(n);return request_error("invalid NES OAM inspection");}
    if(frame_path) {
        static const char header[]="P6\n256 240\n255\n";
        const uint8_t *frame=nes_frame(n);
        if(!frame || !write_new_file(frame_path,header,sizeof(header)-1,frame,256*240*3)) {
            nes_destroy(n);return request_error("cannot create NES frame or no frame has completed");
        }
    }
    size_t sample_count=0;
    if(audio_path) {
        const int16_t *samples=nes_audio(n,&sample_count);uint32_t sample_bytes=(uint32_t)(sample_count*2),riff_size=sample_bytes+36;
        uint8_t wav[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                         0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
        for(unsigned i=0;i<4;i++){wav[4+i]=(uint8_t)(riff_size>>(i*8));wav[40+i]=(uint8_t)(sample_bytes>>(i*8));}
        int16_t zero=0;if(!samples) samples=&zero;
        if(!write_new_file(audio_path,(const char *)wav,sizeof(wav),(const uint8_t *)samples,sample_bytes)) {
            nes_destroy(n);return request_error("cannot create NES audio capture");
        }
    }
    printf("{\"status\":\"%s\",\"machine\":\"nes\",\"profile\":\"%s\","
           "\"steps\":%" PRIu64 ",\"cpu_cycles\":%" PRIu64 ",\"ppu_frames\":%" PRIu64
           ",\"audio_captured\":%s,\"audio_samples\":%zu,\"audio_truncated\":%s,\"dmc_used\":%s,\"apu_writes\":%" PRIu64 ",\"oam_dma\":%" PRIu64
           ",\"pc\":%u,\"a\":%u,\"x\":%u,\"y\":%u,\"sp\":%u,\"flags\":%u,\"rom_sha256\":\"",
           status,cartridge.mapper==2?"experimental_mapper2_cpu":"experimental_nrom_cpu",state.steps,state.cpu_cycles,state.ppu_frames,audio_path?"true":"false",sample_count,state.audio_truncated?"true":"false",
           state.dmc_used?"true":"false",state.apu_writes,state.dma_transfers,(unsigned)state.pc,(unsigned)state.a,(unsigned)state.x,
           (unsigned)state.y,(unsigned)state.sp,(unsigned)state.flags);
    for(size_t i=0;i<32;i++) printf("%02x",(unsigned)digest[i]);
    putchar('"');
    cartridge=nes_get_cartridge_info(n);
    printf(",\"cartridge\":{\"mapper\":%u,\"submapper\":%u,\"prg_rom\":%zu,\"chr_rom\":%zu,\"prg_ram\":%zu,\"chr_ram\":%zu,\"selected_bank\":%u,\"battery_flag\":%s,\"header_region\":\"%s\",\"region\":\"%s\",\"region_override\":%s}",
           cartridge.mapper,cartridge.submapper,cartridge.prg_rom,cartridge.chr_rom,cartridge.prg_ram,cartridge.chr_ram,cartridge.selected_bank,
           cartridge.battery_flag?"true":"false",cartridge.header_region==1?"pal":cartridge.header_region==2?"multi":"ntsc",cartridge.pal?"pal":"ntsc",region?"true":"false");
    if(state.fault) printf(",\"fault\":{\"operation\":\"%s\",\"address\":%u,\"opcode\":%u}",state.fault,(unsigned)state.fault_address,(unsigned)state.fault_opcode);
    if(event_count) printf(",\"controller_schedule\":{\"scheduled\":%zu,\"accepted\":%zu,\"pending\":%zu}",event_count,next_event,event_count-next_event);
    if(controller_set[1] || have_event[1]) {
        size_t scheduled[2]={0,0},accepted[2]={0,0};
        for(size_t i=0;i<event_count;i++){++scheduled[events[i].port];if(i<next_event)++accepted[events[i].port];}
        printf(",\"controllers\":[{\"initial\":%u,\"scheduled\":%zu,\"accepted\":%zu,\"pending\":%zu},{\"initial\":%u,\"scheduled\":%zu,\"accepted\":%zu,\"pending\":%zu}]",controller[0],scheduled[0],accepted[0],scheduled[0]-accepted[0],controller[1],scheduled[1],accepted[1],scheduled[1]-accepted[1]);
    }
    if(video_path) printf(",\"video\":{\"format\":\"y4m\",\"width\":256,\"height\":240,\"fps\":%u,\"frames\":%u,\"frame_limit\":%u,\"truncated\":%s,\"last_cycle\":%" PRIu64 "}",video_fps,video.count,video_limit,video.truncated?"true":"false",video.last_cycle);
    if(inspection_count) {printf(",\"memory\":{\"address\":%u,\"bytes\":\"",inspections[0].address);for(uint32_t i=0;i<inspections[0].length;i++) printf("%02x",bytes[0][i]);fputs("\"}",stdout);}
    if(inspection_count>1) {fputs(",\"memory_regions\":[",stdout);for(size_t j=0;j<inspection_count;j++){if(j)putchar(',');printf("{\"address\":%u,\"bytes\":\"",inspections[j].address);for(uint32_t i=0;i<inspections[j].length;i++)printf("%02x",bytes[j][i]);fputs("\"}",stdout);}putchar(']');}
    if(ppu_len) {printf(",\"ppu_memory\":{\"address\":%u,\"bytes\":\"",ppu_addr);for(uint32_t i=0;i<ppu_len;i++)printf("%02x",ppu_bytes[i]);fputs("\"}",stdout);}
    if(oam_len) {printf(",\"oam\":{\"address\":%u,\"bytes\":\"",oam_addr);for(uint32_t i=0;i<oam_len;i++)printf("%02x",oam_bytes[i]);fputs("\"}",stdout);}
    puts("}");nes_destroy(n);return rc;
}

/* This only queries the Windows API; it neither creates nor runs a guest. */
static int probe_x64_capability(void)
{
    HMODULE library = LoadLibraryExW(L"WinHvPlatform.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HRESULT status;
    BOOL present = FALSE;
    UINT32 written = 0;
    if (library == NULL) {
        status = HRESULT_FROM_WIN32(GetLastError());
    } else {
        typedef HRESULT (WINAPI *capability_fn)(UINT32, void *, UINT32, UINT32 *);
        capability_fn query = (capability_fn)(void *)GetProcAddress(library, "WHvGetCapability");
        if (query == NULL) status = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        else status = query(0, &present, sizeof(present), &written);
        FreeLibrary(library);
    }
    if (FAILED(status)) {
        printf("{\"status\":\"x64_capability_error\",\"backend\":\"whpx\",\"hresult\":\"0x%08" PRIx32
               "\",\"implemented\":false,\"guest_executed\":false}\n", (uint32_t)status);
        return 1;
    }
    if (written < sizeof(present)) {
        puts("{\"status\":\"x64_capability_error\",\"backend\":\"whpx\",\"error\":\"incomplete capability response\",\"implemented\":false,\"guest_executed\":false}");
        return 1;
    }
    printf("{\"status\":\"x64_capability\",\"backend\":\"whpx\",\"hypervisor_present\":%s,"
           "\"implemented\":false,\"guest_executed\":false}\n", present ? "true" : "false");
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    for (int i=1;i+1<argc;i+=2) if(wcscmp(argv[i],L"--machine")==0 && wcscmp(argv[i+1],L"nes")==0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        return run_nes(argc,argv);
    }
    if (argc >= 2 && wcscmp(argv[1], L"--probe-x64-software") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        uint32_t n[1];
        if (argc != 5 || wcscmp(argv[3],L"--steps") != 0 || !parse_fields(argv[4],n,1) || !n[0] || n[0]>1000000)
            return request_error("usage: --probe-x64-software DISK --steps 1..1000000");
        return soft_x64_probe(argv[2],n[0]);
    }
    if (argc >= 2 && wcscmp(argv[1], L"--map-x64-media") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        uint64_t base = 0;
        if (argc != 5 || wcscmp(argv[3], L"--load-base") != 0 || !parse_u64(argv[4], &base))
            return request_error("usage: --map-x64-media DISK --load-base ADDRESS");
        return map_x64_media(argv[2], base);
    }
    if (argc >= 2 && wcscmp(argv[1], L"--convert-capture") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        if (argc != 5 || wcscmp(argv[3], L"--output") != 0 || !*argv[2] || !*argv[4])
            return request_error("usage: --convert-capture INPUT --output OUTPUT");
        const char *format = NULL;
        const char *error = convert_capture(argv[2], argv[4], &format);
        if (error != NULL) return request_error(error);
        printf("{\"status\":\"capture_converted\",\"format\":\"%s\"}\n", format);
        return 0;
    }
    if (argc == 2 && wcscmp(argv[1], L"--x64-capability") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        return probe_x64_capability();
    }
    if (argc >= 2 && wcscmp(argv[1], L"--verify-x64-media") == 0) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        if ((argc != 3 && argc != 5) || (argc == 5 && wcscmp(argv[3], L"--expect-efi") != 0))
            return request_error("usage: --verify-x64-media DISK [--expect-efi EFI_FILE]");
        return verify_x64_media(argv[2], argc == 5 ? argv[4] : NULL);
    }
    if (argc == 2 && wcscmp(argv[1], L"--version") == 0) {
        puts("BootHush build " __DATE__ " " __TIME__ "; identify exact executable with SHA-256");
        return 0;
    }
    if (argc == 2 && wcscmp(argv[1], L"--help") == 0) {
        puts("BootHush Windows emulator\n"
             "Inspect only: --verify-x64-media DISK [--expect-efi EFI_FILE] (no execution)\n"
             "Map only: --map-x64-media DISK --load-base ADDRESS (no execution)\n"
             "Experimental x64 instruction probe: --probe-x64-software DISK --steps N (no firmware)\n"
             "Experimental NES: --machine nes --rom IMAGE.nes --steps N --timeout-ms N [--region auto|ntsc|pal] [--controller1 MASK] [--controller2 MASK] [--controller-event STEP:MASK] [--controller2-event STEP:MASK] [--until-pc ADDRESS] [--inspect ADDRESS:LENGTH] [--inspect-ppu ADDRESS:LENGTH] [--inspect-oam ADDRESS:LENGTH] [--frame PATH] [--audio PATH] [--video PATH]\n"
             "Host capability only: --x64-capability (does not run a guest)\n"
             "Required: --machine mac128k|pc286 --cycles N --timeout-ms N\n"
             "Images: --rom PATH --floppy PATH --hard-disk PATH --disk-out PATH --ega-rom PATH\n"
             "Stop: --until ADDRESS:WIDTH:VALUE --until-pc ADDRESS --until-post BYTE --cancel-event NAME\n"
             "Output: --inspect ADDRESS:LENGTH (repeatable) --frame PATH (Mac PBM, PC PPM), JSON on stdout\n"
             "Video: --video PATH [--video-fps 1..60] [--video-frames 1..3600] [--video-start CYCLE] (Y4M)\n"
             "Convert: --convert-capture INPUT --output OUTPUT (.gif, .mp4 or .png; bundled FFmpeg)\n"
             "Mac input: --mouse-motion CYCLE:DX:DY --mouse-button CYCLE:STATE\n"
             "  --mouse-phase CYCLE:X:Y --key CYCLE:CODE:STATE\n"
             "  --window-mouse CYCLE:X:Y:ACTION (0 move, 1 press, 2 held move, 3 release)\n"
             "  --live SCALE --mouse-record PATH --mouse-replay PATH --mouse-witness PATH\n"
             "  --mouse-witness-range FIRST:LAST --mouse-witness-detail 1 --mouse-witness-memory ADDRESS:LENGTH\n"
             "  --mouse-edge-cycles N --mouse-vbl-guard N\n"
             "  --mouse-dcd-invert MASK\n"
             "  --input-after ADDRESS:WIDTH:VALUE (scheduled input offsets from readiness)\n"
             "PC input: --scancode CYCLE:BYTE; --ega-font PATH (256 8x14 glyphs)\n"
             "PC test: --fail-floppy-read CYLINDER:HEAD:SECTOR (built-in firmware)\n"
             "PC trace: --pc-witness PATH --pc-witness-memory ADDRESS:LENGTH\n"
             "  --pc-witness-io PORT:LENGTH --pc-witness-cycles FIRST:LAST\n"
             "Limits: cycles 1..4000000000, timeout-ms 1..3600000, 1024 input events,\n"
             "  16 inspect ranges, 4096 bytes per range, 16384 bytes total.\n"
             "Exit 0 completed, 1 timeout/cancel/fault or unmet condition, 2 invalid request/output.\n"
             "Only --live opens a visible window. See README.md for formats and timing.");
        return 0;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    struct request request = {0};
    const char *error = parse_request(argc, argv, &request);
    if (error != NULL) {
        return request_error(error);
    }
    struct machine machine = {0};
    error = create_machine(&machine, &request);
    if (error == NULL && request.fail_floppy_read_set &&
        !pc286_fail_floppy_read(machine.pc, request.fail_floppy_read[0], request.fail_floppy_read[1], request.fail_floppy_read[2]))
        error = "cannot configure floppy read failure";
    if (error == NULL && request.mouse_witness_path != NULL &&
        !mac128k_start_mouse_witness(machine.mac, request.mouse_witness_first, request.mouse_witness_last)) {
        error = "cannot initialize the mouse device witness";
    }
    if (error == NULL && request.mouse_witness_detail)
        mac128k_mouse_witness_detailed(machine.mac, true);
    if (error == NULL && request.mouse_witness_memory.length &&
        !mac128k_mouse_witness_memory(machine.mac, request.mouse_witness_memory.address,
                                      request.mouse_witness_memory.length))
        error = "cannot initialize mouse witness RAM filter";
    if (error == NULL && request.pc_witness_path != NULL &&
        !pc286_start_witness(machine.pc, request.pc_witness_memory.address,
                             request.pc_witness_memory.address + request.pc_witness_memory.length,
                             request.pc_witness_io.address, request.pc_witness_io.address + request.pc_witness_io.length,
                             request.pc_witness_first, request.pc_witness_last)) {
        error = "cannot initialize the 286 witness";
    }
    if (error != NULL) {
        destroy_machine(&machine);
        return request_error(error);
    }

    HANDLE cancel_event = NULL;
    if (request.cancel_event != NULL) {
        cancel_event = OpenEventW(SYNCHRONIZE, FALSE, request.cancel_event);
        if (cancel_event == NULL) {
            destroy_machine(&machine);
            return request_error("cannot open cancellation event");
        }
    }
    struct mouse_session mouse = {0};
    mouse.mac = machine.mac;
    mouse.witness_detail = request.mouse_witness_detail;
    error = open_mouse_session(&mouse, &request);
    if (error != NULL) {
        close_mouse_session(&mouse);
        if (cancel_event != NULL) {
            CloseHandle(cancel_event);
        }
        destroy_machine(&machine);
        return request_error(error);
    }
    video_capture capture = {0};
    video_capture *video = NULL;
    if (request.video_path != NULL) {
        if (!video_open(&capture, request.video_path, machine.mac != NULL,
                        request.video_fps, request.video_limit, request.video_start)) {
            close_mouse_session(&mouse);
            if (cancel_event != NULL) CloseHandle(cancel_event);
            destroy_machine(&machine);
            return request_error("cannot create or initialize requested video");
        }
        video = &capture;
    }
    const BOOL console_handler = SetConsoleCtrlHandler(on_console_event, TRUE);
    struct run_result result = run_machine(&machine, &request, &mouse, cancel_event, video);
    if (video != NULL && !video_finish(video, video->failed))
        result = (struct run_result){"error", "cannot close requested video", 2};
    if (mouse.window != NULL) {
        mac_live_window_destroy(mouse.window);
        mouse.window = NULL;
    }
    if (cancel_event != NULL) {
        CloseHandle(cancel_event);
    }
    const struct machine_state state = get_state(&machine);
    uint8_t bytes[MAX_INSPECT_TOTAL];
    const uint8_t *inspection_bytes = NULL;
    size_t offset = 0;
    for (size_t index = 0; index < request.inspect_count; ++index) {
        if (!read_ram(&machine, request.inspect[index].address, bytes + offset, request.inspect[index].length)) {
            result = (struct run_result){"error", "cannot read requested guest RAM", 2};
            offset = 0;
            break;
        }
        offset += request.inspect[index].length;
    }
    if (offset != 0) { inspection_bytes = bytes; }
    if (request.frame_path != NULL && !capture_frame(&machine, request.frame_path)) {
        result = (struct run_result){"error", "cannot create or write requested frame", 2};
    }
    if (request.disk_out_path != NULL && result.exit_code == 0) {
        const uint8_t *disk = pc286_hard_disk(machine.pc);
        if (disk == NULL ||
            !write_new_file(request.disk_out_path, NULL, 0, disk, PC286_HARD_DISK_SIZE)) {
            result = (struct run_result){"error", "cannot create or write requested disk snapshot", 2};
        }
    }
    if (mouse.record != NULL) {
        size_t size;
        const uint8_t *data = mac_mouse_trace_serialize(mouse.record, state.cycles, &size);
        if (data == NULL || !write_new_file(request.mouse_record_path, NULL, 0, data, (DWORD)size)) {
            result = (struct run_result){"error", "cannot create or write requested mouse trace", 2};
        }
    }
    if (request.mouse_witness_path != NULL && !write_mouse_witness(machine.mac, &request, &mouse, state.cycles)) {
        result = (struct run_result){"error", "cannot create or write requested mouse witness", 2};
    }
    if (request.pc_witness_path != NULL && !write_pc_witness(machine.pc, &request, state.cycles)) {
        result = (struct run_result){"error", "cannot create or write requested 286 witness", 2};
    }
    print_result(result.status, request.machine, state, result.error, inspection_bytes, request.inspect, request.inspect_count,
                 &mouse, video);
    close_mouse_session(&mouse);
    destroy_machine(&machine);
    if (console_handler) {
        SetConsoleCtrlHandler(on_console_event, FALSE);
    }
    return result.exit_code;
}
