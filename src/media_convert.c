#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "media_convert.h"

enum { PATH_CAPACITY = 32768, CONVERT_TIMEOUT_MS = 120000 };

const char *convert_capture(const wchar_t *input, const wchar_t *output, const char **format)
{
    wchar_t executable[PATH_CAPACITY], in[PATH_CAPACITY], out[PATH_CAPACITY];
    wchar_t temporary[PATH_CAPACITY], command[PATH_CAPACITY];
    DWORD n = GetModuleFileNameW(NULL, executable, PATH_CAPACITY);
    if (!n || n >= PATH_CAPACITY) return "cannot locate BootHush executable";
    wchar_t *slash = wcsrchr(executable, L'\\');
    if (!slash || (size_t)(slash - executable) + 20 >= PATH_CAPACITY)
        return "cannot locate bundled FFmpeg";
    wcscpy(slash + 1, L"ffmpeg\\ffmpeg.exe");
    DWORD attributes = GetFileAttributesW(executable);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "bundled FFmpeg is missing";
    n = GetFullPathNameW(input, PATH_CAPACITY, in, NULL);
    if (!n || n >= PATH_CAPACITY) return "invalid capture input path";
    attributes = GetFileAttributesW(in);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "capture input file does not exist";
    n = GetFullPathNameW(output, PATH_CAPACITY, out, NULL);
    if (!n || n >= PATH_CAPACITY) return "invalid capture output path";
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
        return "capture output already exists";
    const wchar_t *extension = wcsrchr(out, L'.');
    const wchar_t *options;
    if (extension && _wcsicmp(extension, L".gif") == 0) {
        *format = "gif"; options = L"-c:v gif -f gif";
    } else if (extension && _wcsicmp(extension, L".mp4") == 0) {
        *format = "mp4"; options = L"-c:v libx264 -pix_fmt yuv420p -movflags +faststart -f mp4";
    } else if (extension && _wcsicmp(extension, L".png") == 0) {
        *format = "png"; options = L"-frames:v 1 -c:v png -update 1 -f image2";
    } else return "capture output must end in .gif, .mp4 or .png";

    HANDLE reserved = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        int length = swprintf(temporary, PATH_CAPACITY, L"%ls.boothush-%lu-%u.tmp",
                              out, (unsigned long)GetCurrentProcessId(), attempt);
        if (length < 0 || length >= PATH_CAPACITY) return "capture output path is too long";
        reserved = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (reserved != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return "cannot create temporary capture output";
    }
    if (reserved == INVALID_HANDLE_VALUE) return "cannot reserve temporary capture output";
    CloseHandle(reserved);
    int length = swprintf(command, PATH_CAPACITY, L"\"%ls\" -hide_banner -loglevel error -nostdin -y -i \"%ls\" -an %ls \"%ls\"",
                          executable, in, options, temporary);
    if (length < 0 || length >= PATH_CAPACITY) {
        DeleteFileW(temporary);
        return "conversion command is too long";
    }
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) {
        DeleteFileW(temporary);
        return "cannot start bundled FFmpeg";
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, CONVERT_TIMEOUT_MS);
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    } else if (!GetExitCodeProcess(process.hProcess, &code)) code = 1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0 || code != 0) {
        DeleteFileW(temporary);
        return wait == WAIT_TIMEOUT ? "bundled FFmpeg conversion timed out" : "bundled FFmpeg conversion failed";
    }
    if (!MoveFileW(temporary, out)) {
        DeleteFileW(temporary);
        return "cannot create capture output; destination may already exist";
    }
    return NULL;
}
