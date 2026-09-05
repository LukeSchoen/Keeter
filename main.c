#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef HANDLE HWAVEIN;
typedef UINT MMRESULT;
typedef WORD INTERNET_PORT;
typedef struct wavehdr_tag {
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD dwFlags;
    DWORD dwLoops;
    struct wavehdr_tag *lpNext;
    DWORD_PTR reserved;
} WAVEHDR;
typedef struct waveformat_tag {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
} WAVEFORMAT;
typedef struct waveformatex_tag {
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX;
typedef struct waveincapsa_tag {
    WORD wMid;
    WORD wPid;
    UINT vDriverVersion;
    CHAR szPname[32];
    DWORD dwFormats;
    WORD wChannels;
    WORD wReserved1;
} WAVEINCAPSA;

#define CALLBACK_EVENT 0x00050000
#define WAVE_FORMAT_PCM 1
#define WAVE_MAPPER ((UINT_PTR)-1)
#define MMSYSERR_NOERROR 0
#define WHDR_DONE 0x00000001

MMRESULT WINAPI waveInOpen(HWAVEIN *, UINT_PTR, const WAVEFORMATEX *, DWORD_PTR, DWORD_PTR, DWORD);
UINT WINAPI waveInGetNumDevs(void);
MMRESULT WINAPI waveInGetDevCapsA(UINT_PTR, WAVEINCAPSA *, UINT);
MMRESULT WINAPI waveInPrepareHeader(HWAVEIN, WAVEHDR *, UINT);
MMRESULT WINAPI waveInUnprepareHeader(HWAVEIN, WAVEHDR *, UINT);
MMRESULT WINAPI waveInAddBuffer(HWAVEIN, WAVEHDR *, UINT);
MMRESULT WINAPI waveInStart(HWAVEIN);
MMRESULT WINAPI waveInStop(HWAVEIN);
MMRESULT WINAPI waveInReset(HWAVEIN);
MMRESULT WINAPI waveInClose(HWAVEIN);

#define PARAKEET_CLI_REL "..\\parakeet-v0.5.0-bin-win-cpu-x64\\parakeet-cli.exe"
#define PARAKEET_MODEL_REL "..\\parakeet-v0.5.0-bin-win-cpu-x64\\models\\tdt-0.6b-v2-q8_0.gguf"
#define DOWNLOAD_CMD_NAME "download.cmd"
#define SAMPLE_RATE 16000
#define MAX_SAMPLE_RATE 48000
#define CHANNELS 1
#define BITS_PER_SAMPLE 16
#define CHUNK_MS 50
#define PREROLL_MS 300
#define RELEASE_TAIL_MS 500
#define CHUNK_BYTES (MAX_SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8) * CHUNK_MS / 1000)

typedef struct Options {
    int debug_save_wav;
    char ini_path[MAX_PATH];
    char debug_wav_path[MAX_PATH];
    char parakeet_cli_path[MAX_PATH];
    char parakeet_model_path[MAX_PATH];
} Options;

typedef struct Recorder {
    HWAVEIN wave;
    WAVEHDR headers[4];
    char buffers[4][CHUNK_BYTES];
    HANDLE event;
    BYTE *data;
    DWORD len;
    DWORD cap;
    BYTE *preroll;
    DWORD preroll_cap;
    DWORD preroll_len;
    DWORD preroll_pos;
    int capturing;
    DWORD sample_rate;
    DWORD chunk_bytes;
    UINT_PTR device_id;
} Recorder;

static void recorder_poll(Recorder *r);
static void recorder_stop(Recorder *r);
static int recorder_begin_capture(Recorder *r);
static void recorder_end_capture(Recorder *r);
static int recorder_ensure_running(Recorder *r);
static void log_line(const char *message);

static void get_exe_dir_path(char *out, DWORD out_len, const char *name) {
    char *slash;
    DWORD len = GetModuleFileNameA(NULL, out, out_len);
    if (len == 0 || len >= out_len) {
        if (out_len) out[0] = 0;
        return;
    }
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    else out[0] = 0;
    if (name) strncat(out, name, out_len - strlen(out) - 1);
}

static void get_exe_relative_path(char *out, DWORD out_len, const char *relative_path) {
    get_exe_dir_path(out, out_len, NULL);
    if (relative_path && out[0]) strncat(out, relative_path, out_len - strlen(out) - 1);
}

static int file_exists(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_absolute_path(const char *path) {
    if (!path || !path[0]) return 0;
    if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')) return 1;
    return path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}

static void get_configured_path(char *out, DWORD out_len, const char *ini_path, const char *key, const char *fallback_rel) {
    char value[MAX_PATH];
    GetPrivateProfileStringA("parakeet", key, "", value, sizeof(value), ini_path);
    if (value[0]) {
        if (is_absolute_path(value)) {
            strncpy(out, value, out_len - 1);
            out[out_len - 1] = 0;
        } else {
            get_exe_relative_path(out, out_len, value);
        }
    } else {
        get_exe_relative_path(out, out_len, fallback_rel);
    }
}

static void append_command_arg(char *cmd, DWORD cmd_len, const char *arg) {
    DWORD used = (DWORD)strlen(cmd);
    if (used + 4 >= cmd_len) return;
    if (used) cmd[used++] = ' ';
    cmd[used++] = '"';
    while (*arg && used + 3 < cmd_len) {
        if (*arg == '"') cmd[used++] = '\\';
        cmd[used++] = *arg++;
    }
    cmd[used++] = '"';
    cmd[used] = 0;
}

static void load_options(Options *options) {
    FILE *f;
    char msg[MAX_PATH + 80];
    memset(options, 0, sizeof(*options));
    get_exe_dir_path(options->ini_path, sizeof(options->ini_path), "options.ini");
    get_exe_dir_path(options->debug_wav_path, sizeof(options->debug_wav_path), "keeter_capture_debug.wav");

    f = fopen(options->ini_path, "rb");
    if (!f) {
        f = fopen(options->ini_path, "wb");
        if (f) {
            fputs("[debug]\r\nsaveWav=0\r\n\r\n[parakeet]\r\ncli=..\\parakeet-v0.5.0-bin-win-cpu-x64\\parakeet-cli.exe\r\nmodel=..\\parakeet-v0.5.0-bin-win-cpu-x64\\models\\tdt-0.6b-v2-q8_0.gguf\r\n", f);
            fclose(f);
            sprintf(msg, "Created options file: %s", options->ini_path);
            log_line(msg);
        } else {
            sprintf(msg, "Could not create options file: %s", options->ini_path);
            log_line(msg);
        }
    } else {
        fclose(f);
    }

    options->debug_save_wav = GetPrivateProfileIntA("debug", "saveWav", 0, options->ini_path) != 0;
    get_configured_path(options->parakeet_cli_path, sizeof(options->parakeet_cli_path), options->ini_path, "cli", PARAKEET_CLI_REL);
    get_configured_path(options->parakeet_model_path, sizeof(options->parakeet_model_path), options->ini_path, "model", PARAKEET_MODEL_REL);
    sprintf(msg, "Debug saveWav is %s.", options->debug_save_wav ? "on" : "off");
    log_line(msg);
}

static int run_download_script(void) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char script_path[MAX_PATH];
    char cmd[MAX_PATH * 2 + 32] = "cmd.exe /c ";
    DWORD exit_code = 1;

    get_exe_dir_path(script_path, sizeof(script_path), DOWNLOAD_CMD_NAME);
    if (!file_exists(script_path)) {
        log_line("First-run setup failed: download.cmd was not found.");
        return 0;
    }

    append_command_arg(cmd, sizeof(cmd), script_path);
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    log_line("Parakeet assets are missing; running first-run downloader...");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        log_line("First-run setup failed: could not start download.cmd.");
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code != 0) {
        log_line("First-run setup failed: download.cmd exited with an error.");
        return 0;
    }
    log_line("First-run downloader completed.");
    return 1;
}

static int ensure_parakeet_assets(const Options *options) {
    if (file_exists(options->parakeet_cli_path) && file_exists(options->parakeet_model_path)) return 1;
    if (!run_download_script()) return 0;
    if (file_exists(options->parakeet_cli_path) && file_exists(options->parakeet_model_path)) return 1;
    log_line("First-run setup completed, but the configured Parakeet files are still missing.");
    return 0;
}

static void log_line(const char *message) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    printf("[%02u:%02u:%02u] %s\n", t.wHour, t.wMinute, t.wSecond, message);
    fflush(stdout);
}

static int append_audio(Recorder *r, const BYTE *src, DWORD n) {
    BYTE *p;
    DWORD newcap;
    if (r->len + n > r->cap) {
        newcap = r->cap ? r->cap * 2 : 262144;
        while (newcap < r->len + n) newcap *= 2;
        p = (BYTE *)realloc(r->data, newcap);
        if (!p) return 0;
        r->data = p;
        r->cap = newcap;
    }
    memcpy(r->data + r->len, src, n);
    r->len += n;
    return 1;
}

static void append_preroll(Recorder *r, const BYTE *src, DWORD n) {
    DWORD first;
    if (!r->preroll || !r->preroll_cap || !n) return;
    if (n >= r->preroll_cap) {
        memcpy(r->preroll, src + n - r->preroll_cap, r->preroll_cap);
        r->preroll_pos = 0;
        r->preroll_len = r->preroll_cap;
        return;
    }
    first = r->preroll_cap - r->preroll_pos;
    if (first > n) first = n;
    memcpy(r->preroll + r->preroll_pos, src, first);
    if (n > first) memcpy(r->preroll, src + first, n - first);
    r->preroll_pos = (r->preroll_pos + n) % r->preroll_cap;
    if (r->preroll_len + n < r->preroll_cap) r->preroll_len += n;
    else r->preroll_len = r->preroll_cap;
}

static int append_preroll_to_capture(Recorder *r) {
    DWORD start;
    DWORD first;
    if (!r->preroll_len) return 1;
    start = (r->preroll_pos + r->preroll_cap - r->preroll_len) % r->preroll_cap;
    first = r->preroll_cap - start;
    if (first > r->preroll_len) first = r->preroll_len;
    if (!append_audio(r, r->preroll + start, first)) return 0;
    if (r->preroll_len > first && !append_audio(r, r->preroll, r->preroll_len - first)) return 0;
    return 1;
}

static int recorder_start(Recorder *r) {
    WAVEFORMATEX fmt;
    int i;
    int rate_i;
    DWORD rates[] = {16000, 48000, 44100, 32000, 22050};
    UINT dev_count;
    UINT_PTR device_ids[32];
    int device_count = 0;
    int device_i;
    MMRESULT mm;
    char msg[MAX_PATH + 80];
    memset(r, 0, sizeof(*r));
    r->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!r->event) {
        log_line("Failed to create recorder event.");
        return 0;
    }

    dev_count = waveInGetNumDevs();
    if (!dev_count) {
        log_line("No microphone input devices reported by WinMM.");
        CloseHandle(r->event);
        r->event = NULL;
        return 0;
    }
    for (i = 0; i < (int)dev_count && device_count < 32; i++) device_ids[device_count++] = (UINT_PTR)i;

    for (device_i = 0; device_i < device_count; device_i++) {
        for (rate_i = 0; rate_i < (int)(sizeof(rates) / sizeof(rates[0])); rate_i++) {
            memset(&fmt, 0, sizeof(fmt));
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = CHANNELS;
            fmt.nSamplesPerSec = rates[rate_i];
            fmt.wBitsPerSample = BITS_PER_SAMPLE;
            fmt.nBlockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);
            fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

            mm = waveInOpen(&r->wave, device_ids[device_i], &fmt, (DWORD_PTR)r->event, 0, CALLBACK_EVENT);
            if (mm == MMSYSERR_NOERROR) {
                r->sample_rate = rates[rate_i];
                r->chunk_bytes = r->sample_rate * CHANNELS * (BITS_PER_SAMPLE / 8) * CHUNK_MS / 1000;
                r->device_id = device_ids[device_i];
                sprintf(msg, "Opened microphone device %ld at %lu Hz mono PCM.", (long)r->device_id, (unsigned long)r->sample_rate);
                log_line(msg);
                break;
            }
            sprintf(msg, "Could not open microphone device %ld at %lu Hz; WinMM error %u.", (long)device_ids[device_i], (unsigned long)rates[rate_i], (unsigned)mm);
            log_line(msg);
        }
        if (r->wave) break;
    }
    if (!r->wave) {
        log_line("Failed to open microphone input at any tested sample rate.");
        CloseHandle(r->event);
        r->event = NULL;
        return 0;
    }
    r->preroll_cap = r->sample_rate * CHANNELS * (BITS_PER_SAMPLE / 8) * PREROLL_MS / 1000;
    r->preroll = (BYTE *)malloc(r->preroll_cap);
    r->cap = r->preroll_cap + 262144;
    r->data = (BYTE *)malloc(r->cap);
    if (!r->preroll || !r->data) {
        log_line("Failed to allocate recorder buffers.");
        recorder_stop(r);
        return 0;
    }
    for (i = 0; i < 4; i++) {
        r->headers[i].lpData = r->buffers[i];
        r->headers[i].dwBufferLength = r->chunk_bytes;
        waveInPrepareHeader(r->wave, &r->headers[i], sizeof(WAVEHDR));
        waveInAddBuffer(r->wave, &r->headers[i], sizeof(WAVEHDR));
    }
    if (waveInStart(r->wave) != MMSYSERR_NOERROR) {
        log_line("Failed to start microphone recording.");
        return 0;
    }
    return 1;
}

static int recorder_ensure_running(Recorder *r) {
    static DWORD last_notice_tick = 0;
    if (r->wave) return 1;
    if (recorder_start(r)) return 1;
    if ((LONG)(GetTickCount() - last_notice_tick) > 5000) {
        log_line("Waiting for a microphone to become available...");
        last_notice_tick = GetTickCount();
    }
    Sleep(500);
    return 0;
}

static void print_mic_devices(void) {
    UINT i, n = waveInGetNumDevs();
    char msg[256];
    sprintf(msg, "waveInGetNumDevs reports %u input device(s).", n);
    log_line(msg);
    for (i = 0; i < n; i++) {
        WAVEINCAPSA caps;
        MMRESULT mm = waveInGetDevCapsA(i, &caps, sizeof(caps));
        if (mm == MMSYSERR_NOERROR) {
            sprintf(msg, "Device %u: %s, channels=%u, formats=0x%08lx", i, caps.szPname, caps.wChannels, (unsigned long)caps.dwFormats);
        } else {
            sprintf(msg, "Device %u: waveInGetDevCapsA failed with WinMM error %u.", i, (unsigned)mm);
        }
        log_line(msg);
    }
}

static int mic_test(void) {
    Recorder r;
    DWORD start;
    DWORD captured;
    char msg[256];
    print_mic_devices();
    log_line("Mic simulation: trying to record for 1000 ms without hotkey.");
    if (!recorder_start(&r)) return 1;
    if (!recorder_begin_capture(&r)) {
        recorder_stop(&r);
        return 1;
    }
    start = GetTickCount();
    while (GetTickCount() - start < 1000) {
        recorder_poll(&r);
        Sleep(5);
    }
    recorder_end_capture(&r);
    captured = r.len;
    recorder_stop(&r);
    sprintf(msg, "Mic simulation captured %lu bytes.", (unsigned long)captured);
    log_line(msg);
    return captured > 0 ? 0 : 2;
}

static void recorder_poll(Recorder *r) {
    int i;
    WaitForSingleObject(r->event, 0);
    for (i = 0; i < 4; i++) {
        if (r->headers[i].dwFlags & WHDR_DONE) {
            if (r->headers[i].dwBytesRecorded) {
                BYTE *pcm = (BYTE *)r->headers[i].lpData;
                DWORD n = r->headers[i].dwBytesRecorded;
                append_preroll(r, pcm, n);
                if (r->capturing) append_audio(r, pcm, n);
            }
            r->headers[i].dwFlags &= ~WHDR_DONE;
            r->headers[i].dwBytesRecorded = 0;
            waveInAddBuffer(r->wave, &r->headers[i], sizeof(WAVEHDR));
        }
    }
}

static int recorder_begin_capture(Recorder *r) {
    r->len = 0;
    r->capturing = 0;
    if (!append_preroll_to_capture(r)) {
        log_line("Failed to copy pre-roll audio.");
        return 0;
    }
    r->capturing = 1;
    return 1;
}

static void recorder_end_capture(Recorder *r) {
    r->capturing = 0;
}

static void recorder_stop(Recorder *r) {
    int i;
    if (r->wave) {
        waveInStop(r->wave);
        waveInReset(r->wave);
        recorder_poll(r);
        for (i = 0; i < 4; i++) waveInUnprepareHeader(r->wave, &r->headers[i], sizeof(WAVEHDR));
        waveInClose(r->wave);
    }
    if (r->event) CloseHandle(r->event);
    free(r->data);
    free(r->preroll);
    memset(r, 0, sizeof(*r));
}

static int write_wav(const char *path, const BYTE *pcm, DWORD pcm_len, DWORD sample_rate) {
    FILE *f = fopen(path, "wb");
    DWORD riff_len = 36 + pcm_len;
    DWORD byte_rate = sample_rate * CHANNELS * (BITS_PER_SAMPLE / 8);
    WORD block_align = CHANNELS * (BITS_PER_SAMPLE / 8);
    DWORD subchunk1 = 16;
    WORD audio_format = 1;
    if (!f) return 0;
    fwrite("RIFF", 1, 4, f); fwrite(&riff_len, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&subchunk1, 4, 1, f); fwrite(&audio_format, 2, 1, f);
    fwrite(&(WORD){CHANNELS}, 2, 1, f); fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f); fwrite(&block_align, 2, 1, f); fwrite(&(WORD){BITS_PER_SAMPLE}, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&pcm_len, 4, 1, f); fwrite(pcm, 1, pcm_len, f);
    fclose(f);
    return 1;
}

static char *transcribe_file(const char *path, const Options *options) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    char cmd[MAX_PATH * 3 + 80] = "";
    char *response = NULL;
    DWORD got, used = 0, cap = 0;
    DWORD exit_code = 1;

    if (!ensure_parakeet_assets(options)) {
        log_line("Transcription failed: Parakeet assets are not ready.");
        return NULL;
    }

    if (!file_exists(options->parakeet_cli_path)) {
        log_line("Transcription failed: parakeet-cli.exe was not found.");
        return NULL;
    }
    if (!file_exists(options->parakeet_model_path)) {
        log_line("Transcription failed: configured Parakeet .gguf model was not found.");
        return NULL;
    }

    log_line("Running Parakeet CLI...");
    append_command_arg(cmd, sizeof(cmd), options->parakeet_cli_path);
    strcat(cmd, " transcribe --model ");
    append_command_arg(cmd, sizeof(cmd), options->parakeet_model_path);
    strcat(cmd, " --input ");
    append_command_arg(cmd, sizeof(cmd), path);
    strcat(cmd, " --decoder tdt");

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        log_line("Transcription failed: could not create stdout pipe.");
        return NULL;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = write_pipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        log_line("Transcription failed: could not start parakeet-cli.exe.");
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return NULL;
    }
    CloseHandle(write_pipe);

    for (;;) {
        char tmp[4096];
        if (!ReadFile(read_pipe, tmp, sizeof(tmp), &got, NULL) || got == 0) break;
        if (used + got + 1 > cap) {
            char *p;
            cap = cap ? cap * 2 : 8192;
            while (cap < used + got + 1) cap *= 2;
            p = (char *)realloc(response, cap);
            if (!p) {
                free(response);
                response = NULL;
                break;
            }
            response = p;
        }
        memcpy(response + used, tmp, got);
        used += got;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(read_pipe);
    if (response) {
        while (used > 0 && (response[used - 1] == '\r' || response[used - 1] == '\n' || response[used - 1] == ' ' || response[used - 1] == '\t')) used--;
        response[used] = 0;
    }
    if (exit_code != 0) {
        log_line("Transcription failed: parakeet-cli.exe exited with an error.");
        free(response);
        return NULL;
    }
    return response;
}

static void paste_text_utf8(const char *text) {
    int wlen;
    HGLOBAL mem;
    WCHAR *dst;
    INPUT inputs[4];
    if (!text || !*text) {
        log_line("No text to paste.");
        return;
    }
    wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    mem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(WCHAR));
    if (!mem) return;
    dst = (WCHAR *)GlobalLock(mem);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, dst, wlen);
    GlobalUnlock(mem);
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, mem);
        CloseClipboard();
    } else {
        GlobalFree(mem);
        log_line("Could not open clipboard.");
        return;
    }
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

static int hotkey_down(void) {
    return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) && ((GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0);
}

int main(int argc, char **argv) {
    Options options;
    Recorder r;
    char wav_path[MAX_PATH];
    char msg[MAX_PATH + 80];
    DWORD ms;
    DWORD audio_bytes;
    printf("keeter v1\n");
    printf("Hold Ctrl+` to talk. Release to transcribe and paste.\n");
    fflush(stdout);
    if (argc > 1 && strcmp(argv[1], "--mic-test") == 0) return mic_test();
    load_options(&options);
    if (argc > 1 && strcmp(argv[1], "--transcribe-test") == 0) {
        char *text;
        if (argc != 3) {
            log_line("Usage: keeter.exe --transcribe-test <audio.wav>");
            return 1;
        }
        text = transcribe_file(argv[2], &options);
        if (!text) return 1;
        puts(text);
        free(text);
        return 0;
    }
    ensure_parakeet_assets(&options);
    GetTempPathA(sizeof(wav_path), wav_path);
    strcat(wav_path, "keeter_capture.wav");
    while (!recorder_ensure_running(&r)) {}
    log_line("Ready. Waiting for Ctrl+`...");
    for (;;) {
        while (!recorder_ensure_running(&r)) {}
        while (!hotkey_down()) {
            recorder_poll(&r);
            if (!waveInGetNumDevs()) {
                log_line("No microphone devices detected; restarting recorder when one is available.");
                recorder_stop(&r);
                break;
            }
            Sleep(5);
        }
        if (!r.wave) continue;
        recorder_poll(&r);
        log_line("Started recording.");
        if (!recorder_begin_capture(&r)) {
            while (hotkey_down()) Sleep(5);
            continue;
        }
        while (hotkey_down()) {
            recorder_poll(&r);
            if (!waveInGetNumDevs()) {
                log_line("Microphone disappeared during recording; restarting recorder.");
                recorder_end_capture(&r);
                recorder_stop(&r);
                break;
            }
            Sleep(5);
        }
        if (!r.wave) continue;
        {
            DWORD tail_until = GetTickCount() + RELEASE_TAIL_MS;
            while ((LONG)(tail_until - GetTickCount()) > 0) {
                recorder_poll(&r);
                if (!waveInGetNumDevs()) {
                    log_line("Microphone disappeared during tail capture; restarting recorder.");
                    recorder_end_capture(&r);
                    recorder_stop(&r);
                    break;
                }
                Sleep(5);
            }
        }
        if (!r.wave) continue;
        recorder_end_capture(&r);
        audio_bytes = r.len;
        ms = audio_bytes / (r.sample_rate * CHANNELS * (BITS_PER_SAMPLE / 8) / 1000);
        sprintf(msg, "Finished recording: %lu ms, %lu bytes.", (unsigned long)ms, (unsigned long)audio_bytes);
        log_line(msg);
        if (r.len > r.sample_rate / 2 && write_wav(wav_path, r.data, r.len, r.sample_rate)) {
            char *text = transcribe_file(wav_path, &options);
            if (text && *text) {
                printf("AI heard: %s\n", text);
                fflush(stdout);
            } else {
                log_line("AI heard nothing.");
            }
            paste_text_utf8(text);
            if (text && *text) log_line("Pasted transcript.");
            if (options.debug_save_wav) {
                if (CopyFileA(wav_path, options.debug_wav_path, FALSE)) {
                    sprintf(msg, "Saved debug WAV: %s", options.debug_wav_path);
                    log_line(msg);
                } else {
                    log_line("Failed to save debug WAV.");
                }
            }
            free(text);
        } else {
            log_line("Recording was too short or WAV write failed; skipping transcription.");
        }
        DeleteFileA(wav_path);
        log_line("Ready. Waiting for Ctrl+`...");
        Sleep(150);
    }
    return 0;
}
