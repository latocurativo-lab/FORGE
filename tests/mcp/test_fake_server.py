import pathlib, subprocess, sys

exe = pathlib.Path(sys.argv[1])
python = pathlib.Path(sys.executable)
server = pathlib.Path(__file__).with_name("fake_server.py")
def run(mode, code=0):
    p = subprocess.run([str(exe), str(python), str(server), mode], timeout=8)
    assert p.returncode == code, (mode, p.returncode, code)

run("handshake")
run("normal")
run("malformed", 35) # malformed response is a protocol error
run("oversized", 36)
run("timeout") # the call deadline is handled as expected
run("cancel")
run("crash", 44)
run("noapproval")
