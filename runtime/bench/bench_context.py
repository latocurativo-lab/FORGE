#!/usr/bin/env python3
"""Reproducible populated-context Gemma 4 benchmark for Windows/llama.cpp."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODEL = ROOT / "models" / "gemma-4-12b-it-Q4_K_M.gguf"
DEFAULT_SERVER = ROOT / "runtime/upstream/llama.cpp/build/bin/Release/llama-server.exe"
DEFAULT_OUT = ROOT / "artifacts/bench"
TARGETS = (4096, 8192, 16384, 32768, 65536, 131072)
MIN_FREE_MIB = 1536
FILLER = (
    "FORGE reproducible context benchmark. Record the numbered observation and "
    "retain its exact value; do not summarize or omit any observation. "
)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def gpu_state() -> dict:
    raw = subprocess.check_output(
        ["nvidia-smi", "--query-gpu=memory.total,memory.used,memory.free,utilization.gpu",
         "--format=csv,noheader,nounits"], text=True, stderr=subprocess.STDOUT
    ).strip().splitlines()[0]
    nums = [int(x.strip()) for x in raw.split(",")]
    return {"total_mib": nums[0], "used_mib": nums[1], "free_mib": nums[2], "utilization_pct": nums[3]}


def compute_processes() -> list[str]:
    """List processes marked compute-only by nvidia-smi; WDDM C+G rows are display apps."""
    result = subprocess.run(
        ["nvidia-smi"],
        capture_output=True, text=True, timeout=15,
    )
    if result.returncode != 0:
        raise RuntimeError((result.stderr or result.stdout).strip() or "nvidia-smi query failed")
    rows = []
    in_process_table = False
    for line in result.stdout.splitlines():
        if "Processes:" in line:
            in_process_table = True
            continue
        if not in_process_table or not line.lstrip().startswith("|"):
            continue
        columns = [part.strip() for part in line.strip().strip("|").split()]
        # WDDM reports graphics clients as C+G and often reports memory as N/A.
        # A standalone C type is the actionable CUDA compute blocker.
        if len(columns) >= 5 and columns[4] == "C":
            rows.append(f"PID {columns[3]} ({' '.join(columns[5:-1])})")
    return rows


def blockers(port: int) -> list[str]:
    found = []
    # Strict by design: do not benchmark alongside another local inference runtime.
    try:
        rows = subprocess.check_output(["tasklist", "/fo", "csv", "/nh"], text=True,
                                       stderr=subprocess.DEVNULL).splitlines()
        for row in rows:
            name = row.split('","', 1)[0].strip('"').lower()
            if any(s in name for s in ("llama-server", "llama-cli", "ollama", "lm studio")):
                found.append(f"inference process present: {name}")
    except (OSError, subprocess.CalledProcessError) as exc:
        found.append(f"cannot verify process list: {exc}")
    try:
        compute = compute_processes()
        found.extend(f"CUDA compute process present: {row}" for row in compute)
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        found.append(f"cannot verify CUDA compute processes: {exc}")
    try:
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", port))
    except OSError:
        found.append(f"127.0.0.1:{port} already in use")
    try:
        state = gpu_state()
        if state["free_mib"] < MIN_FREE_MIB:
            found.append(f"only {state['free_mib']} MiB free VRAM (<{MIN_FREE_MIB} MiB guard)")
    except Exception as exc:
        found.append(f"cannot verify GPU state: {exc}")
    return list(dict.fromkeys(found))


def request(port: int, route: str, data: dict | None, timeout: int = 600) -> dict:
    body = json.dumps(data).encode("utf-8") if data is not None else None
    req = urllib.request.Request(f"http://127.0.0.1:{port}{route}", data=body,
                                 headers={"Content-Type": "application/json"} if body is not None else {})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def token_count(port: int, text: str) -> int:
    result = request(port, "/tokenize", {"content": text, "add_special": False, "parse_special": True})
    return len(result["tokens"])


def formatted_prompt(port: int, content: str) -> str:
    result = request(port, "/apply-template", {
        "messages": [{"role": "user", "content": content}],
        "chat_template_kwargs": {"enable_thinking": False},
    })
    prompt = result.get("prompt")
    if not isinstance(prompt, str):
        raise RuntimeError(f"Unexpected /apply-template response: {result!r}")
    return prompt


def make_prompt(port: int, target: int) -> tuple[str, int, str]:
    # Binary search a deterministic ASCII prefix. Token count is from this exact
    # llama.cpp server/tokenizer, including the Gemma 4 chat template.
    lo, hi = 0, target * 12
    best = ("", 0, "")
    while lo <= hi:
        mid = (lo + hi) // 2
        content = (FILLER * ((mid // len(FILLER)) + 1))[:mid]
        rendered = formatted_prompt(port, content)
        count = token_count(port, rendered)
        if count <= target:
            best = (content, count, rendered)
            lo = mid + 1
        else:
            hi = mid - 1
    # Search the nearby character window in case tokenizer merges crossed the
    # binary-search boundary. If exact target is unreachable, record actual.
    center = len(best[0])
    for n in range(max(0, center - 24), center + 25):
        content = (FILLER * ((n // len(FILLER)) + 1))[:n]
        rendered = formatted_prompt(port, content)
        count = token_count(port, rendered)
        if count == target:
            return content, count, rendered
        if count <= target and count > best[1]:
            best = (content, count, rendered)
    return best


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--targets", type=int, nargs="+", default=list(TARGETS))
    parser.add_argument("--replicates", type=int, default=3)
    parser.add_argument("--experimental-replicates", type=int, default=1,
                        help="replicates for 64K/128K; experiments, not forced")
    parser.add_argument("--max-tokens", type=int, default=16)
    parser.add_argument("--port", type=int, default=18941)
    args = parser.parse_args()
    args.model, args.server, args.out = args.model.resolve(), args.server.resolve(), args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    summary_path = args.out / f"context-{run_id}.json"
    report = {"run_id": run_id, "status": "running", "hardware": "RTX 4060 Ti 16 GB; 32 GB RAM",
              "model_path": str(args.model), "model_sha256": None, "server_path": str(args.server),
              "guard_free_mib": MIN_FREE_MIB, "targets": args.targets, "trials": [], "blockers": []}
    write_json(summary_path, report)

    if not args.model.is_file() or not args.server.is_file():
        report.update(status="blocked", blockers=["model or llama-server binary missing"])
        write_json(summary_path, report)
        return 2
    initial = blockers(args.port)
    report["initial_gpu"] = gpu_state() if not any("cannot verify GPU" in b for b in initial) else None
    if initial:
        report.update(status="blocked", blockers=initial)
        write_json(summary_path, report)
        print("Blocked before launch: " + "; ".join(initial), file=sys.stderr)
        return 2

    report["model_sha256"] = sha256(args.model)
    version = subprocess.run([str(args.server), "--version"], capture_output=True, text=True, timeout=30)
    report["server_version"] = (version.stdout + version.stderr).strip()
    report["server_sha256"] = sha256(args.server)
    report["parameters"] = {"gpu_layers": 99, "flash_attention": "on", "parallel_slots": 1,
                             "threads": 4, "temperature": 0, "seed": 42,
                             "max_tokens": args.max_tokens, "thinking": False, "kv_quant": "default"}

    stop = False
    for target in args.targets:
        reps = args.experimental_replicates if target >= 65536 else args.replicates
        for rep in range(1, reps + 1):
            trial_id = f"{target//1024}k-r{rep}"
            prefix = f"{run_id}-{trial_id}"
            trial = {"trial_id": trial_id, "target_prompt_tokens": target, "replicate": rep,
                     "status": "running", "start_utc": datetime.now(timezone.utc).isoformat()}
            report["trials"].append(trial)
            write_json(summary_path, report)
            # Recheck before every server start; never kill a pre-existing process.
            blocked = blockers(args.port)
            if blocked:
                trial.update(status="blocked", blockers=blocked)
                stop = True
                break
            ctx = target + args.max_tokens + 64
            cmd = [str(args.server), "-m", str(args.model), "-ngl", "99", "-fa", "on",
                   "-c", str(ctx), "-t", "4", "-np", "1", "--host", "127.0.0.1",
                   "--port", str(args.port), "--jinja", "--reasoning", "off"]
            (args.out / f"{prefix}-server-command.json").write_text(json.dumps(cmd, indent=2) + "\n", encoding="utf-8")
            stdout_path, stderr_path = args.out / f"{prefix}-server.out", args.out / f"{prefix}-server.err"
            proc = None
            sampler_stop = threading.Event()
            samples = []
            sampler = None
            try:
                with stdout_path.open("w", encoding="utf-8") as out, stderr_path.open("w", encoding="utf-8") as err:
                    trial["gpu_baseline_before_server"] = gpu_state()
                    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=out, stderr=err)
                    trial["server_pid"] = proc.pid
                    def sample_gpu():
                        while not sampler_stop.is_set():
                            try:
                                point = {"utc": datetime.now(timezone.utc).isoformat(), **gpu_state()}
                                samples.append(point)
                            except Exception as exc:
                                samples.append({"utc": datetime.now(timezone.utc).isoformat(), "error": str(exc)})
                            sampler_stop.wait(0.5)
                    sampler = threading.Thread(target=sample_gpu, name=f"gpu-sampler-{trial_id}", daemon=True)
                    sampler.start()
                    deadline = time.monotonic() + 300
                    while time.monotonic() < deadline:
                        if proc.poll() is not None:
                            raise RuntimeError(f"llama-server exited early ({proc.returncode})")
                        try:
                            request(args.port, "/health", None, timeout=3)
                            break
                        except Exception:
                            time.sleep(1)
                    else:
                        raise TimeoutError("server readiness timeout")

                    # Strict free-memory floor after model load and before eval.
                    before = gpu_state()
                    trial["gpu_before_eval"] = before
                    if before["free_mib"] < MIN_FREE_MIB:
                        trial.update(status="skipped_guard", reason="free VRAM below 1536 MiB before eval")
                        stop = True
                        break
                    content, tokenized, rendered = make_prompt(args.port, target)
                    if tokenized < int(target * 0.98):
                        trial.update(status="prompt_underfilled", actual_tokenized_prompt=tokenized,
                                     reason="tokenizer could not construct a sufficiently populated target")
                        stop = True
                        break
                    (args.out / f"{prefix}-prompt.txt").write_text(content, encoding="utf-8")
                    payload = {"model": "local", "messages": [{"role": "user", "content": content}],
                               "temperature": 0, "seed": 42, "max_tokens": args.max_tokens,
                               "stream": False, "cache_prompt": False,
                               "chat_template_kwargs": {"enable_thinking": False}}
                    started = time.perf_counter()
                    try:
                        response = request(args.port, "/v1/chat/completions", payload, timeout=3600)
                        elapsed = time.perf_counter() - started
                        write_json(args.out / f"{prefix}-response.json", response)
                        usage = response.get("usage", {})
                        timings = response.get("timings", {})
                        choice = (response.get("choices") or [{}])[0]
                        after = gpu_state()
                        trial.update(status="ok", prompt_tokens=usage.get("prompt_tokens"),
                                     tokenized_prompt_tokens=tokenized,
                                     decoded_tokens=usage.get("completion_tokens"),
                                     finish_reason=choice.get("finish_reason"), wall_seconds=elapsed,
                                     prefill_tokens_per_second=timings.get("prompt_per_second"),
                                     decode_tokens_per_second=timings.get("predicted_per_second"),
                                     prefill_ms=timings.get("prompt_ms"), decode_ms=timings.get("predicted_ms"),
                                     gpu_after_eval=after, raw_response_file=f"{prefix}-response.json")
                        if trial["prompt_tokens"] is None or trial["prompt_tokens"] < int(target * .98):
                            trial["status"] = "prompt_underfilled"
                            trial["reason"] = "actual API prompt_tokens below 98% of requested target"
                            stop = True
                        if choice.get("finish_reason") == "error":
                            trial["status"] = "error"
                            stop = True
                    except (urllib.error.URLError, TimeoutError, RuntimeError) as exc:
                        trial.update(status="failed", error=str(exc))
                        stop = True
            except Exception as exc:
                trial.update(status="failed", error=str(exc))
                stop = True
            finally:
                # Only signal the exact child process created for this trial.
                if proc is not None and proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=10)
                sampler_stop.set()
                if sampler is not None:
                    sampler.join(timeout=3)
                (args.out / f"{prefix}-gpu-samples.json").write_text(
                    json.dumps(samples, indent=2) + "\n", encoding="utf-8")
                if samples:
                    valid = [s for s in samples if "used_mib" in s]
                    if valid:
                        trial["gpu_sampled_peak_used_mib"] = max(s["used_mib"] for s in valid)
                        trial["gpu_sampled_min_free_mib"] = min(s["free_mib"] for s in valid)
                    trial["gpu_samples_file"] = f"{prefix}-gpu-samples.json"
                trial["end_utc"] = datetime.now(timezone.utc).isoformat()
                write_json(summary_path, report)
            if stop:
                break
        if stop:
            break
    report["status"] = "stopped_after_failure_or_guard" if stop else "completed"
    report["end_utc"] = datetime.now(timezone.utc).isoformat()
    write_json(summary_path, report)
    print(summary_path)
    return 0 if not stop else 1


if __name__ == "__main__":
    raise SystemExit(main())
