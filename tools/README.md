# FORGE local tools (Windows)

Standalone C library. Build and run its tests with:

```powershell
cmake -S tools -B tools/build
cmake --build tools/build --config Release
ctest --test-dir tools/build -C Release --output-on-failure
```

Link `forge_tools` and include `forge_tools.h`. `ft_open` configures a workspace and explicit `FT_READ`, `FT_LIST`, `FT_WRITE`, and `FT_EXEC` permission bits. The caller must decide those permissions and authorize every call; this library has no interactive approval mechanism. Paths are UTF-16 workspace-relative names using backslashes. Empty path denotes the root for listing. Reads/lists/search use caller buffers and return byte lengths, not NUL-terminated strings. `FT_LIMIT` can leave partial list/search output. Search is a case-sensitive byte substring match over one text file (maximum 1 MiB). `ft_write` remains create-only: it never overwrites, and a failed write can leave a partial file. Listing emits UTF-8 names separated by newlines (ambiguous for unusual names) and entries are not sorted.

### Updating an existing file

Use `ft_replace_approved` only when an update is intended. It requires `FT_WRITE`, a nonzero per-invocation `approved` value, an existing ordinary file, and the exact original file bytes (expected content + byte length). A mismatch returns `FT_STALE` without replacing the target. Replacement bytes are staged in a temporary file in the target directory, flushed with `FlushFileBuffers`, then installed with `ReplaceFileW`. `ft_write` is unchanged and will return an error for an existing target; there is no implicit overwrite path. The API accepts byte buffers, including zero-length content; lengths above `DWORD_MAX` are rejected.

This is not an atomic compare-and-swap. Windows does not provide a byte-conditional `ReplaceFileW`; the implementation compares the original via a handle and rechecks immediately before replacement, but a concurrent actor can still race path replacement between that check and `ReplaceFileW`. A failed staging/replace attempt leaves the old target in place where the filesystem/OS operation guarantees it; power loss, filesystem faults, hostile concurrent namespace mutation, and provider-specific behavior are outside this guarantee. Temp cleanup is best effort on errors.

## Process execution contract

Use `ft_run_approved`, not the deprecated `ft_run` (which always returns `FT_DENIED`). A run requires all of:

* `FT_EXEC` on the workspace;
* an exact case-insensitive executable path match in the caller-provided allowlist;
* `approved != 0` for this invocation;
* `allow_mutation != 0` for this invocation. It defaults to denied because even an allowlisted program can modify files. The library cannot reliably infer every destructive action from arbitrary program arguments; the caller must not set this flag for untrusted commands.

Arguments are a Windows command-line tail, **not shell syntax**. The library does not parse/escape the arguments or enforce an argument allowlist. Do not allowlist shells, interpreters, scripting hosts, or general-purpose utilities for untrusted arguments. Process working directory is always the configured workspace root. Stdout/stderr are drained concurrently, copied up to their independent capacities, and excess output is drained but reported as `FT_LIMIT`. `FT_OK` means the child exited and its exit code was captured; it does not mean exit code zero. A finite nonzero timeout is required. The child is suspended until assigned to a kill-on-close job; timeout or return closes the job and ends remaining job processes. This is process-tree cleanup, **not process isolation**. The child inherits the caller's token, environment, and network access.

## Security limitations (important)

These checks are defense in depth, **not a sandbox or reliable containment boundary**. `ft_open` rejects observed reparse points in the absolute root path's ancestors, and operations validate ordinary path components and observed reparse points. Reads open the final path with `FILE_FLAG_OPEN_REPARSE_POINT` and inspect the opened handle. Nevertheless, parent directory components are reopened by pathname: junction/symlink replacement between validation and use can redirect reads, lists, creates, or replacements. Listing remains pathname-based. Hard links can expose data outside the workspace. The workspace root is not held as a capability handle; root replacement, concurrent mutation, TOCTOU races in compare-and-replace, and partial writes remain possible. Reparse-point checks cannot resolve attacker-controlled races or all filesystem/provider behavior. Device-name edge cases and nonstandard filesystems are not guaranteed.

For hostile inputs, use a restricted Windows token with carefully designed ACLs and an OS isolation boundary (for example a suitably configured Windows Sandbox/VM or a separately installed container/runtime). Job objects alone do not restrict filesystem, registry, or network access. Reliable isolation therefore requires an external privilege/isolation choice and installation/configuration; this library does not claim to provide it.
