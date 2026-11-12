#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "mac128k.h"
#include "mac_live_window.h"
#include "mac_mouse_api.h"
#include "mac_mouse_trace.h"
#include "pc286.h"

enum {
    MAX_INSPECT_BYTES = 4096,
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
    const wchar_t *frame_path;
    const wchar_t *cancel_event;
    const wchar_t *mouse_record_path;
    const wchar_t *mouse_replay_path;
    const wchar_t *mouse_witness_path;
    uint32_t cycle_limit;
    uint32_t timeout_ms;
    struct predicate until;
    struct inspection inspect;
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
    mac_mouse *bridge;
    mac_live_window *window;
    mac_mouse_trace *record;
    mac_mouse_trace *replay;
    size_t replay_index;
    uint64_t accepted;
    uint64_t emitted;
    bool injected_window;
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
        } else if (wcscmp(option, L"--hard-disk") == 0 && request->disk_path == NULL) {
            request->disk_path = value;
        } else if (wcscmp(option, L"--disk-out") == 0 && request->disk_out_path == NULL) {
            request->disk_out_path = value;
        } else if (wcscmp(option, L"--ega-rom") == 0 && request->ega_rom_path == NULL) {
            request->ega_rom_path = value;
        } else if (wcscmp(option, L"--frame") == 0 && request->frame_path == NULL) {
            request->frame_path = value;
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
        } else if (wcscmp(option, L"--inspect") == 0 && request->inspect.length == 0) {
            if (!parse_fields(value, fields, 2) || fields[1] == 0 ||
                fields[1] > MAX_INSPECT_BYTES) {
                return "inspect requires a RAM address and a length between 1 and 4096";
            }
            request->inspect = (struct inspection){fields[0], fields[1]};
        } else {
            return "unknown or repeated option";
        }
    }

    if (request->machine == MACHINE_UNSET ||
        request->cycle_limit == 0 || request->timeout_ms == 0) {
        return "machine, cycles, and timeout-ms are required";
    }
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
    if (request->mouse_witness_range_set && request->mouse_witness_path == NULL) {
        return "mouse-witness-range requires mouse-witness";
    }
    if (!request->mouse_witness_range_set) {
        request->mouse_witness_last = request->cycle_limit;
    }
    if (request->raw_mouse_phases && mouse_bridge) {
        return "raw mouse phases cannot be mixed with relative motion, replay or live input";
    }
    if (request->mouse_record_path != NULL && request->live_scale == 0 && !request->window_input) {
        return "mouse-record requires live input or window-mouse";
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
    if (request->machine == MACHINE_MAC128K) {
        if (request->rom_path == NULL) {
            return "mac128k requires a ROM";
        }
        if (request->stop_at_post || request->disk_path || request->disk_out_path || request->ega_rom_path) {
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
        (request->inspect.length && request->inspect.address > ram_size - request->inspect.length)) {
        return "requested memory is outside physical RAM and video memory";
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

static bool write_mouse_witness(const mac128k *mac, const struct request *request, uint64_t stop)
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
    };
    size_t count;
    uint64_t dropped;
    const mac128k_mouse_witness_event *events = mac128k_mouse_witness(mac, &count, &dropped);
    enum { MAX_ROW_LENGTH = 128 };
    const size_t capacity = sizeof(fields) + (count + 1) * MAX_ROW_LENGTH;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        return false;
    }
    size_t length = (size_t)snprintf(buffer, capacity,
                                    "BHMACMOUSE1 %" PRIu32 " %" PRIu32 " %" PRIu64 " %zu %" PRIu64 "\n%s",
                                    request->mouse_witness_first, request->mouse_witness_last,
                                    stop, count, dropped, fields);
    for (size_t index = 0; index < count && length < capacity; ++index) {
        const mac128k_mouse_witness_event *event = &events[index];
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

static const char *open_mouse_session(struct mouse_session *mouse, const struct request *request)
{
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

static bool queue_mouse_sample(struct mouse_session *mouse, mac_mouse_sample sample)
{
    if (mouse->record != NULL && !mac_mouse_trace_append(mouse->record, sample)) {
        return false;
    }
    if (!mac_mouse_queue_sample(mouse->bridge, sample.cycle, sample.dx, sample.dy,
                               sample.button_changed, sample.pressed)) {
        if (mouse->record != NULL) {
            mac_mouse_trace_pop(mouse->record);
        }
        return false;
    }
    ++mouse->accepted;
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
                         const uint8_t *bytes, struct inspection inspect,
                         const struct mouse_session *mouse)
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
    if (bytes != NULL && inspect.length != 0) {
        printf(",\"memory\":{\"address\":%" PRIu32 ",\"bytes\":\"", inspect.address);
        for (uint32_t index = 0; index < inspect.length; ++index) {
            printf("%02x", (unsigned)bytes[index]);
        }
        fputs("\"}", stdout);
    }
    fputs("}\n", stdout);
}

static int request_error(const char *error)
{
    print_result("error", MACHINE_UNSET, (struct machine_state){0}, error, NULL,
                 (struct inspection){0}, NULL);
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
    while (mouse->replay_index < count && samples[mouse->replay_index].cycle <= through_cycle) {
        if (!queue_mouse_sample(mouse, samples[mouse->replay_index])) {
            return false;
        }
        ++mouse->replay_index;
    }
    return true;
}

static struct run_result run_machine(struct machine *machine, const struct request *request,
                                     struct mouse_session *mouse, HANDLE cancel_event)
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
               request->input_events[next_event].cycle <= state.cycles) {
            const struct input_event event = request->input_events[next_event];
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
            if (mac_mouse_advance(mouse->bridge, state.cycles, &input)) {
                mac128k_set_mouse_phase(machine->mac, input.x_phase, input.y_phase);
                mac128k_set_mouse_button(machine->mac, input.pressed);
                ++mouse->emitted;
            }
            mac_mouse_next_cycle(mouse->bridge, &mouse_deadline);
        }

        const uint64_t previous_cycles = state.cycles;
        uint32_t slice = use_predicate ? 1 : CYCLE_SLICE;
        slice = slice_before(slice, previous_cycles, cycle_limit);
        slice = slice_before(slice, previous_cycles, live_target);
        slice = slice_before(slice, previous_cycles, mouse_deadline);
        if (next_event < request->input_event_count) {
            slice = slice_before(slice, previous_cycles, request->input_events[next_event].cycle);
        }
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
    struct image rom = {0}, floppy = {0}, disk = {0}, ega_rom = {0};
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
        }
        if (machine->mac == NULL && machine->pc == NULL) {
            error = "cannot initialize machine or invalid boot image";
        }
    }
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

int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--version") == 0) {
        puts("BootHush build " __DATE__ " " __TIME__ "; identify exact executable with SHA-256");
        return 0;
    }
    if (argc == 2 && wcscmp(argv[1], L"--help") == 0) {
        puts("BootHush Windows emulator\n"
             "Required: --machine mac128k|pc286 --cycles N --timeout-ms N\n"
             "Images: --rom PATH --floppy PATH --hard-disk PATH --disk-out PATH --ega-rom PATH\n"
             "Stop: --until ADDRESS:WIDTH:VALUE --until-pc ADDRESS --until-post BYTE --cancel-event NAME\n"
             "Output: --inspect ADDRESS:LENGTH --frame PATH (Mac PBM, PC PPM), JSON on stdout\n"
             "Mac input: --mouse-motion CYCLE:DX:DY --mouse-button CYCLE:STATE\n"
             "  --mouse-phase CYCLE:X:Y --key CYCLE:CODE:STATE\n"
             "  --window-mouse CYCLE:X:Y:ACTION (0 move, 1 press, 2 held move, 3 release)\n"
             "  --live SCALE --mouse-record PATH --mouse-replay PATH --mouse-witness PATH\n"
             "  --mouse-witness-range FIRST:LAST --mouse-edge-cycles N --mouse-vbl-guard N\n"
             "  --mouse-dcd-invert MASK\n"
             "PC input: --scancode CYCLE:BYTE\n"
             "Limits: cycles 1..4000000000, timeout-ms 1..3600000, 1024 input events, 4096 inspected bytes.\n"
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
    if (error == NULL && request.mouse_witness_path != NULL &&
        !mac128k_start_mouse_witness(machine.mac, request.mouse_witness_first, request.mouse_witness_last)) {
        error = "cannot initialize the mouse device witness";
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
    error = open_mouse_session(&mouse, &request);
    if (error != NULL) {
        close_mouse_session(&mouse);
        if (cancel_event != NULL) {
            CloseHandle(cancel_event);
        }
        destroy_machine(&machine);
        return request_error(error);
    }
    const BOOL console_handler = SetConsoleCtrlHandler(on_console_event, TRUE);
    struct run_result result = run_machine(&machine, &request, &mouse, cancel_event);
    if (mouse.window != NULL) {
        mac_live_window_destroy(mouse.window);
        mouse.window = NULL;
    }
    if (cancel_event != NULL) {
        CloseHandle(cancel_event);
    }
    const struct machine_state state = get_state(&machine);
    uint8_t bytes[MAX_INSPECT_BYTES];
    const uint8_t *inspection_bytes = NULL;
    if (request.inspect.length != 0) {
        if (read_ram(&machine, request.inspect.address, bytes, request.inspect.length)) {
            inspection_bytes = bytes;
        } else {
            result = (struct run_result){"error", "cannot read requested guest RAM", 2};
        }
    }
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
    if (request.mouse_witness_path != NULL && !write_mouse_witness(machine.mac, &request, state.cycles)) {
        result = (struct run_result){"error", "cannot create or write requested mouse witness", 2};
    }
    print_result(result.status, request.machine, state, result.error, inspection_bytes, request.inspect,
                 &mouse);
    close_mouse_session(&mouse);
    destroy_machine(&machine);
    if (console_handler) {
        SetConsoleCtrlHandler(on_console_event, FALSE);
    }
    return result.exit_code;
}
