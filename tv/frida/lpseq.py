import subprocess, sys, time, frida
import re
PRELUDE = re.search(r'PRELUDE = """(.*?)"""', open('run.py').read(), re.S).group(1)
S = sys.argv[1]
T = ["adb", "-s", "192.168.14.11:5555"]
dev = frida.get_device_manager().add_remote_device("127.0.0.1:27042")
sc = dev.attach("Gadget").create_script(PRELUDE + open("lpseq.js").read())
msgs = []
sc.on("message", lambda m, d: (print(m.get("payload", m), flush=True), msgs.append(m.get("payload"))))
sc.load()
def shot(name):
    subprocess.run(T + ["shell", "screencap", "-p", "/sdcard/h.png"], capture_output=True)
    subprocess.run(T + ["pull", "/sdcard/h.png", f"{S}/{name}.png"], capture_output=True)
time.sleep(0.5)
for n in range(4):
    shot(f"ab{n}_off"); sc.post({"type": "on", "k": n}); time.sleep(0.3)
    shot(f"ab{n}_on"); sc.post({"type": "off"}); time.sleep(0.3)
