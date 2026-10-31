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
verifier = pathlib.Path(sys.argv[2])
mcp_script = pathlib.Path(__file__).with_name("fake_server.py")
python = pathlib.Path(sys.executable)
requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length))
        requests.append(body)
        if self.path != "/v1/chat/completions":
            self.send_error(404)
            return
        if len(requests) == 1:
            response = {"choices": [{"finish_reason": "tool_calls", "message": {
                "content": None, "tool_calls": [{"id": "mcp-call-1", "type": "function",
                    "function": {"name": "mcp.fixture.echo", "arguments": "{\"text\":\"hello\"}"}}]}}]}
        else:
            response = {"choices": [{"finish_reason": "stop", "message": {"content": "done"}}]}
        encoded = json.dumps(response).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, *_args):
        pass


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


port = free_port()
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
lines = queue.Queue()
with tempfile.TemporaryDirectory(prefix="forge-mcp-integration-") as tmp:
    workspace = pathlib.Path(tmp) / "workspace"
    workspace.mkdir()
    checkpoint = pathlib.Path(tmp) / "checkpoint.json"
    config = {
        "type": "start", "task": "Use the configured MCP fixture and finish.",
        "workspace": str(workspace), "base_url": f"http://127.0.0.1:{port}",
        "checkpoint": str(checkpoint), "allow_exe": [str(verifier)],
        "verifier": {"executable": str(verifier), "args": ""},
        "mcp_servers": [{"id": "fixture", "executable": str(python),
            "args": ["-u", str(mcp_script), "normal"], "allowlist": [str(python)], "timeout_ms": 5000}],
    }
    proc = subprocess.Popen([str(forge)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8")

    def read_stdout():
        for line in proc.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=read_stdout, daemon=True).start()
    proc.stdin.write(json.dumps(config) + "\n")
    proc.stdin.flush()
    events = []
    deadline = time.monotonic() + 40
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
                reply = {"type": "approval", "request_id": event["request_id"], "approved": True}
                proc.stdin.write(json.dumps(reply) + "\n")
                proc.stdin.flush()
            if event.get("type") == "exit":
                # The logical terminal event is emitted before forge.exe has
                # finished its orderly stdin-reader shutdown. Close the pipe
                # so the agent's JSONL reader observes EOF without forced
                # CancelSynchronousIo on the CRT fgetc thread.
                proc.stdin.close()
                break
        else:
            raise AssertionError("timed out waiting for FORGE MCP integration run")
        code = proc.wait(timeout=5)
        stderr = proc.stderr.read()
        assert code == 0, (code, stderr, events)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)

server.shutdown()
server.server_close()

assert len(requests) == 2, requests
offered = requests[0].get("tools", [])
assert any(t.get("function", {}).get("name") == "mcp.fixture.echo" for t in offered), offered
assert not any("ignore safety" in json.dumps(t) for t in offered), offered
assert [e.get("capability") for e in events if e.get("type") == "approval_required"] == ["MCP_LAUNCH", "MCP_CALL", "VERIFY"], events
tool_results = [e for e in events if e.get("type") == "tool_result"]
assert tool_results and "untrusted output" in json.dumps(tool_results[0]), events
assert any(e.get("type") == "verification_passed" for e in events), events
print("PASS test_agent_mcp_integration: launch/call/verifier approvals and tool round-trip")
