import http.server
import json
import pathlib
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time


forge = pathlib.Path(sys.argv[1])
requests = []


def response_for(index):
    if index == 1:
        # Deliberately try a mutation before inspecting the project. The agent
        # must deny this locally without asking for mutation approval.
        call = {"id": "write-first", "type": "function", "function": {
            "name": "write_file", "arguments": json.dumps({"path": "src/app.tsx", "content": "// uninspected"})}}
        return {"choices": [{"finish_reason": "tool_calls", "message": {"content": "I will create the file now.", "tool_calls": [call]}}]}
    if index == 2:
        call = {"id": "inspect-first", "type": "function", "function": {
            "name": "list_dir", "arguments": json.dumps({"path": ""})}}
        return {"choices": [{"finish_reason": "tool_calls", "message": {"content": None, "tool_calls": [call]}}]}
    return {"choices": [{"finish_reason": "stop", "message": {
        "content": "Here is the code you can paste into your project."}}]}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length))
        requests.append(body)
        if self.path != "/v1/chat/completions":
            self.send_error(404)
            return
        encoded = json.dumps(response_for(len(requests))).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, *_args):
        pass


with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
lines = queue.Queue()

with tempfile.TemporaryDirectory(prefix="forge-agentic-policy-") as tmp:
    workspace = pathlib.Path(tmp) / "workspace"
    workspace.mkdir()
    proc = subprocess.Popen([str(forge)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)

    def read_stdout():
        for line in proc.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=read_stdout, daemon=True).start()
    start = {
        "type": "start", "mode": "coding",
        "task": "ottimo, fai un piano ed eseguilo: crea il componente nel progetto.",
        "workspace": str(workspace), "base_url": f"http://127.0.0.1:{port}",
        "allow_exe": [], "verifier": {"executable": "", "args": ""},
        "mcp_servers": [], "history": [], "instructions": "",
    }
    proc.stdin.write(json.dumps(start) + "\n")
    proc.stdin.flush()
    events = []
    deadline = time.monotonic() + 30
    try:
        while time.monotonic() < deadline:
            try:
                raw = lines.get(timeout=0.25)
            except queue.Empty:
                if proc.poll() is not None:
                    break
                continue
            if raw is None:
                break
            event = json.loads(raw)
            events.append(event)
            if event.get("type") == "approval_required":
                # The only approved operation in this fixture is read-only
                # list_dir; no mutation/command approval is granted.
                if event.get("tool") != "list_dir" or event.get("mutation"):
                    proc.kill()
                    raise AssertionError(f"unexpected approval request: {event}")
                proc.stdin.write(json.dumps({"type": "approval", "request_id": event["request_id"], "approved": True}) + "\n")
                proc.stdin.flush()
            if event.get("type") == "exit":
                proc.stdin.close()
                break
        proc.wait(timeout=10)
    finally:
        server.shutdown()

    types = [event.get("type") for event in events]
    first_write_denied = next((i for i, event in enumerate(events)
                               if event.get("type") == "tool_denied" and event.get("tool") == "write_file"), None)
    first_approval = next((i for i, event in enumerate(events) if event.get("type") == "approval_required"), None)
    assert first_write_denied is not None, types
    assert first_approval is not None and first_write_denied < first_approval, events
    assert "incomplete" in types, types
    assert "assistant_final" not in types, types
    assert proc.returncode != 0, proc.returncode
    assert not list(workspace.rglob("*")), list(workspace.rglob("*"))
    assert len(requests) == 5, len(requests)
    assert all(request.get("max_tokens") == -1 for request in requests)
    assert any("inspect the workspace first" in json.dumps(request.get("messages", [])).lower()
               for request in requests[2:]), "agentic recovery instruction was not replayed"

print("agentic policy: pre-inspection write denied; plan-only response not completed; recovery bounded and visible")
