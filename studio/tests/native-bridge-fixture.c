#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int read_line(char *line, size_t size) {
    return fgets(line, (int)size, stdin) != NULL;
}

static void emit(const char *json) {
    puts(json);
    fflush(stdout);
}

int main(void) {
    char line[8192];
    if (!read_line(line, sizeof(line)) || strstr(line, "\"type\":\"start\"") == NULL) return 10;

    /* Deliberately emit before the start_agent invoke has necessarily resolved. */
    emit("{\"type\":\"fixture_started\"}");
    emit("{\"type\":\"approval_required\",\"tool\":\"fixture-write\",\"path\":\"safe.txt\",\"args\":\"fixture payload\",\"call_id\":\"call-approve\",\"capability\":\"write\",\"mutation\":true,\"request_id\":\"fixture-approve\"}");
    if (!read_line(line, sizeof(line)) || strstr(line, "\"type\":\"approval\"") == NULL || strstr(line, "\"request_id\":\"fixture-approve\"") == NULL || strstr(line, "\"approved\":true") == NULL) return 11;
    emit("{\"type\":\"fixture_approval_received\",\"approved\":true}");

    emit("{\"type\":\"approval_required\",\"tool\":\"fixture-reject\",\"path\":\"safe.txt\",\"args\":\"reject this request\",\"call_id\":\"call-reject\",\"capability\":\"write\",\"mutation\":true,\"request_id\":\"fixture-reject\"}");
    if (!read_line(line, sizeof(line)) || strstr(line, "\"type\":\"approval\"") == NULL || strstr(line, "\"request_id\":\"fixture-reject\"") == NULL || strstr(line, "\"approved\":false") == NULL) return 12;
    emit("{\"type\":\"fixture_approval_received\",\"approved\":false}");

    emit("{\"type\":\"fixture_waiting_for_cancel\"}");
    if (!read_line(line, sizeof(line)) || strstr(line, "\"type\":\"cancel\"") == NULL) return 13;
    emit("{\"type\":\"fixture_cancel_received\"}");
    Sleep(500);
    emit("{\"type\":\"fixture_cancelled\"}");
    return 0;
}
