#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int contains(const char *haystack, const char *needle)
{
    return haystack && strstr(haystack, needle) != NULL;
}

static char *wide_to_utf8(const wchar_t *value)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                NULL, 0, NULL, NULL);
    char *out;
    if (n <= 0) return NULL;
    out = (char *)malloc((size_t)n);
    if (!out || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                     out, n, NULL, NULL)) {
        free(out);
        return NULL;
    }
    return out;
}

int main(int argc, char **argv)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE child_stdin_read = NULL, parent_stdin_write = NULL;
    HANDLE parent_stdout_read = NULL, child_stdout_write = NULL;
    wchar_t temp_root[MAX_PATH], temp_name[MAX_PATH], checkpoint_w[MAX_PATH];
    char *workspace = NULL, *checkpoint = NULL, *request = NULL, *command = NULL;
    char output[65536];
    DWORD nread, nwritten, wait_result, exit_code = 0;
    size_t used = 0;
    int ok = 0;

    if (argc != 2) { fprintf(stderr, "usage: test_studio_start <forge.exe>\n"); return 2; }
    if (!GetTempPathW(MAX_PATH, temp_root) || !GetTempFileNameW(temp_root, L"fgt", 0, temp_name)) {
        fprintf(stderr, "cannot create temporary workspace path\n"); return 2;
    }
    DeleteFileW(temp_name);
    if (!CreateDirectoryW(temp_name, NULL)) { fprintf(stderr, "cannot create temporary workspace\n"); return 2; }
    if (swprintf_s(checkpoint_w, MAX_PATH, L"%s\\checkpoint.json", temp_name) < 0) goto cleanup;
    workspace = wide_to_utf8(temp_name);
    checkpoint = wide_to_utf8(checkpoint_w);
    if (!workspace || !checkpoint) goto cleanup;

    /* Use forge_json's safe string escaping indirectly via a narrow helper:
       JSON strings need backslash, quote, and control escaping. Temp paths do
       not contain quotes/control characters; encode backslashes explicitly. */
    {
        size_t cap = strlen(workspace) * 2 + strlen(checkpoint) * 2 + 512;
        char *w = (char *)malloc(strlen(workspace) * 2 + 1);
        char *c = (char *)malloc(strlen(checkpoint) * 2 + 1);
        size_t wi = 0, ci = 0;
        if (!w || !c) { free(w); free(c); goto cleanup; }
        for (const char *p = workspace; *p; p++) { if (*p == '\\') w[wi++] = '\\'; w[wi++] = *p; }
        w[wi] = 0;
        for (const char *p = checkpoint; *p; p++) { if (*p == '\\') c[ci++] = '\\'; c[ci++] = *p; }
        c[ci] = 0;
        request = (char *)malloc(cap);
        if (request) snprintf(request, cap,
            "\xEF\xBB\xBF{\"type\":\"start\",\"task\":\"minimal startup regression\","
            "\"workspace\":\"%s\",\"checkpoint\":\"%s\","
            "\"base_url\":\"http://127.0.0.1:1\"}\n", w, c);
        free(w); free(c);
    }
    if (!request) goto cleanup;

    if (!CreatePipe(&child_stdin_read, &parent_stdin_write, &sa, 0) ||
        !CreatePipe(&parent_stdout_read, &child_stdout_write, &sa, 0)) {
        fprintf(stderr, "CreatePipe failed: %lu\n", (unsigned long)GetLastError()); goto cleanup;
    }
    SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_read; si.hStdOutput = child_stdout_write; si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&pi, sizeof(pi));
    {
        size_t len = strlen(argv[1]) + 3;
        command = (char *)malloc(len);
        if (!command) goto cleanup;
        snprintf(command, len, "\"%s\"", argv[1]);
    }
    if (!CreateProcessA(argv[1], command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", (unsigned long)GetLastError()); goto cleanup;
    }
    CloseHandle(child_stdin_read); child_stdin_read = NULL;
    CloseHandle(child_stdout_write); child_stdout_write = NULL;
    if (!WriteFile(parent_stdin_write, request, (DWORD)strlen(request), &nwritten, NULL) ||
        nwritten != strlen(request)) {
        fprintf(stderr, "failed to send JSONL start\n"); goto child_cleanup;
    }
    CloseHandle(parent_stdin_write); parent_stdin_write = NULL;

    while (ReadFile(parent_stdout_read, output + used,
                    (DWORD)(sizeof(output) - used - 1), &nread, NULL) && nread) {
        used += nread;
        if (used >= sizeof(output) - 1) break;
        output[used] = 0;
    }
    output[used] = 0;
    wait_result = WaitForSingleObject(pi.hProcess, 15000);
    if (wait_result != WAIT_OBJECT_0) {
        fprintf(stderr, "forge did not exit after local connection refusal\n");
        TerminateProcess(pi.hProcess, 124);
        goto child_cleanup;
    }
    GetExitCodeProcess(pi.hProcess, &exit_code);
    if (!contains(output, "\"type\":\"start\"") ||
        !contains(output, "\"type\":\"generation_started\"")) {
        fprintf(stderr, "startup events missing; exit=%lu output=%s\n", (unsigned long)exit_code, output);
        goto child_cleanup;
    }
    if (exit_code == 0) {
        fprintf(stderr, "unexpected success without a reachable runtime\n");
        goto child_cleanup;
    }
    ok = 1;
    puts("OK test_studio_start: JSONL startup emits events and exits cleanly on unavailable runtime");

child_cleanup:
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
cleanup:
    if (child_stdin_read) CloseHandle(child_stdin_read);
    if (parent_stdin_write) CloseHandle(parent_stdin_write);
    if (parent_stdout_read) CloseHandle(parent_stdout_read);
    if (child_stdout_write) CloseHandle(child_stdout_write);
    if (checkpoint) DeleteFileW(checkpoint_w);
    if (workspace) { RemoveDirectoryW(temp_name); }
    free(workspace); free(checkpoint); free(request); free(command);
    return ok ? 0 : 1;
}
