#ifndef FORGE_TOOLS_H
#define FORGE_TOOLS_H
#include <stddef.h>
#include <windows.h>

enum { FT_READ = 1, FT_LIST = 2, FT_WRITE = 4, FT_EXEC = 8 };
typedef struct { wchar_t root[MAX_PATH]; unsigned permissions; } ft_workspace;
typedef enum { FT_OK, FT_DENIED, FT_INVALID, FT_IO, FT_LIMIT, FT_TIMEOUT, FT_STALE, FT_CANCELLED } ft_result;
/* All paths are UTF-16, workspace-relative, and use ordinary Windows names. */
ft_result ft_open(ft_workspace *ws, const wchar_t *root, unsigned permissions);
ft_result ft_read(const ft_workspace *ws, const wchar_t *path, void *out, size_t capacity, size_t *length);
ft_result ft_write(const ft_workspace *ws, const wchar_t *path, const void *data, size_t length);
/* Replace an existing file only after explicit approval and exact original-byte match.
   The original and replacement are byte buffers (not NUL-terminated strings). */
ft_result ft_replace_approved(const ft_workspace *ws, const wchar_t *path,
    const void *expected_original, size_t expected_length,
    const void *replacement, size_t replacement_length, int approved);
/* Lists immediate names, or matching lines in one file, into a UTF-8 byte buffer. */
ft_result ft_list(const ft_workspace *ws, const wchar_t *path, char *out, size_t capacity, size_t *length);
ft_result ft_search(const ft_workspace *ws, const wchar_t *path, const char *needle, char *out, size_t capacity, size_t *length);
/* Exact executable allowlist match and explicit per-call approval are required.
   args is a Windows command line tail (not shell syntax); cwd is always workspace root.
   Output is captured up to each capacity; truncated streams return FT_LIMIT. */
ft_result ft_run_approved_cancelable(const ft_workspace *ws, const wchar_t *exe,
    const wchar_t *const *allowlist, size_t allowlist_count, int approved,
    int allow_mutation, const wchar_t *args, DWORD timeout_ms,
    volatile int *cancel,
    char *stdout_buf, size_t stdout_capacity, size_t *stdout_length,
    char *stderr_buf, size_t stderr_capacity, size_t *stderr_length,
    DWORD *exit_code);
ft_result ft_run_approved(const ft_workspace *ws, const wchar_t *exe,
    const wchar_t *const *allowlist, size_t allowlist_count, int approved,
    int allow_mutation, const wchar_t *args, DWORD timeout_ms,
    char *stdout_buf, size_t stdout_capacity, size_t *stdout_length,
    char *stderr_buf, size_t stderr_capacity, size_t *stderr_length,
    DWORD *exit_code);
/* Deprecated unrestricted interface is intentionally denied. */
ft_result ft_run(const ft_workspace *ws, const wchar_t *exe, const wchar_t *args, DWORD timeout_ms, DWORD *exit_code);
#endif
