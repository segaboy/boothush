#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "mac128k.h"
#include "pc286.h"

enum {
    MAX_INSPECT_BYTES = 4096,
    MAX_INPUT_EVENTS = 1024,
    MAX_CYCLES = 1000000000,
    MAX_TIMEOUT_MS = 60000,
    CYCLE_SLICE = 256,
};

struct predicate {
    uint32_t address;
    uint32_t width;
    uint32_t value;
};

struct inspection {
    uint32_t address;
    uint32_t length;
};

enum input_kind { INPUT_BUTTON, INPUT_PHASE, INPUT_MAC_KEY, INPUT_PC_SCAN };

struct input_event {
    uint32_t cycle;
    enum input_kind kind;
    uint8_t first;
    uint8_t second;
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
    uint32_t cycle_limit;
    uint32_t timeout_ms;
    struct predicate until;
    struct inspection inspect;
    bool stop_at_pc;
    uint32_t target_pc;
    bool stop_at_post;
    uint32_t target_post;
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

static bool add_input_event(struct request *request, enum input_kind kind, const wchar_t *value)
{
    const size_t count = request->input_event_count;
    const bool two_arguments = kind == INPUT_PHASE || kind == INPUT_MAC_KEY;
    uint32_t fields[3] = {0};
    if (count == MAX_INPUT_EVENTS || !parse_fields(value, fields, two_arguments ? 3 : 2) ||
        (count != 0 && fields[0] < request->input_events[count - 1].cycle)) {
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
    }
    request->input_events[count] = (struct input_event){
        fields[0], kind, (uint8_t)fields[1], (uint8_t)fields[2]};
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
        } else if (wcscmp(option, L"--mouse-button") == 0) {
            if (!add_input_event(request, INPUT_BUTTON, value)) {
                return "invalid mouse-button cycle:state event or input order";
            }
        } else if (wcscmp(option, L"--mouse-phase") == 0) {
            if (!add_input_event(request, INPUT_PHASE, value)) {
                return "invalid mouse-phase cycle:x:y event or input order";
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
                return "cycles must be between 1 and 1000000000";
            }
            request->cycle_limit = fields[0];
        } else if (wcscmp(option, L"--timeout-ms") == 0 && request->timeout_ms == 0) {
            if (!parse_fields(value, fields, 1) || fields[0] == 0 || fields[0] > MAX_TIMEOUT_MS) {
                return "timeout-ms must be between 1 and 60000";
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
        if ((request->input_events[index].kind == INPUT_PC_SCAN) != (request->machine == MACHINE_PC286)) {
            return "use scancode for pc286 and key or mouse events for mac128k";
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
                         const uint8_t *bytes, struct inspection inspect)
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
    print_result("error", MACHINE_UNSET, (struct machine_state){0}, error, NULL, (struct inspection){0});
    return 2;
}

static bool apply_input(struct machine *machine, struct input_event event)
{
    switch (event.kind) {
    case INPUT_BUTTON:
        mac128k_set_mouse_button(machine->mac, event.first != 0);
        return true;
    case INPUT_PHASE:
        mac128k_set_mouse_phase(machine->mac, event.first, event.second);
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

static struct run_result run_machine(struct machine *machine, const struct request *request,
                                     HANDLE cancel_event)
{
    LARGE_INTEGER frequency, start;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&start)) {
        return (struct run_result){"error", "cannot start monotonic timer", 2};
    }
    const double timeout_ticks = (double)frequency.QuadPart * request->timeout_ms / 1000.0;
    const bool use_predicate = request->until.width != 0 || request->stop_at_pc || request->stop_at_post;
    if (machine->pc != NULL && request->input_event_count != 0) {
        pc286_expect_input(machine->pc, true);
    }
    struct machine_state state = get_state(machine);
    size_t next_event = 0;

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
        if ((double)(now.QuadPart - start.QuadPart) >= timeout_ticks) {
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
        if (state.cycles >= request->cycle_limit) {
            return (struct run_result){"cycle_limit", NULL, use_predicate ? 1 : 0};
        }

        while (next_event < request->input_event_count &&
               request->input_events[next_event].cycle <= state.cycles) {
            if (!apply_input(machine, request->input_events[next_event])) {
                return (struct run_result){"machine_fault", "guest keyboard rejected an input event", 1};
            }
            ++next_event;
            if (machine->pc != NULL && next_event == request->input_event_count) {
                pc286_expect_input(machine->pc, false);
            }
        }

        const uint64_t previous_cycles = state.cycles;
        uint32_t slice = (uint32_t)(request->cycle_limit - previous_cycles);
        if (next_event < request->input_event_count) {
            const uint32_t to_event = request->input_events[next_event].cycle - (uint32_t)previous_cycles;
            if (to_event < slice) {
                slice = to_event;
            }
        }
        if (use_predicate) {
            slice = 1;
        } else if (slice > CYCLE_SLICE) {
            slice = CYCLE_SLICE;
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
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    struct request request = {0};
    const char *error = parse_request(argc, argv, &request);
    if (error != NULL) {
        return request_error(error);
    }
    struct machine machine = {0};
    error = create_machine(&machine, &request);
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
    const BOOL console_handler = SetConsoleCtrlHandler(on_console_event, TRUE);
    struct run_result result = run_machine(&machine, &request, cancel_event);
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
    print_result(result.status, request.machine, state, result.error, inspection_bytes, request.inspect);
    destroy_machine(&machine);
    if (console_handler) {
        SetConsoleCtrlHandler(on_console_event, FALSE);
    }
    return result.exit_code;
}
