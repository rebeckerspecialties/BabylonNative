"""Exercise the installed portal preset with a live XR rAF, normal Back, and an in-process relaunch."""
import argparse
import pathlib
import re
import subprocess
import tempfile
import time


PACKAGE = "com.android.babylonnative.playground"
ACTIVITY = PACKAGE + "/.PlaygroundActivity"


def run(serial):
    adb = ["adb", "-s", serial]

    def shell(*args):
        return subprocess.check_output([*adb, "shell", *args], timeout=30, text=True)

    def wait_for(path, pid, marker, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for line in path.read_text(errors="replace").splitlines():
                if line.split()[2:3] == [pid]:
                    if "Fatal signal" in line or "FATAL EXCEPTION" in line:
                        raise RuntimeError(line)
                    if marker in line:
                        return line
            time.sleep(.2)
        raise RuntimeError(f"PID {pid}: timed out waiting for {marker}")

    prior = None
    for cycle in range(2):
        inventory = shell("ps", "-A", "-T", "-o", "PID,TID,NAME")
        if not inventory.lstrip().startswith("PID") or re.search(r"(?:/|\s)(NativeWebGPUCts|cts_runner)\s*$", inventory, re.M):
            raise RuntimeError("Cannot establish exclusive headset access")
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "activity.log"
            with path.open("wb") as out:
                reader = subprocess.Popen([*adb, "logcat", "-T", "1", "-v", "threadtime", "-b", "main", "-b", "events", "-b", "crash",
                                           "-s", "BabylonNative", "am_on_create_called", "am_on_destroy_called", "libc", "AndroidRuntime"], stdout=out)
                try:
                    shell("am", "start", "-W", "-n", ACTIVITY)
                    pid = shell("pidof", PACKAGE).strip()
                    if not pid.isdecimal() or (prior and prior != pid):
                        raise RuntimeError(f"Process changed during lifecycle check: {prior} -> {pid}")
                    wait_for(path, pid, "am_on_create_called", 10)
                    wait_for(path, pid, "Submitted stereo frame to OpenXR compositor", 75)
                    print(wait_for(path, pid, "native-xr-portal:stats:frames=", 75), flush=True)
                    if ACTIVITY not in shell("dumpsys", "activity", "activities").split("mResumedActivity:")[-1].splitlines()[0]:
                        raise RuntimeError("Not sending Back: portal is not the resumed Activity")
                    shell("input", "keyevent", "KEYCODE_BACK")
                    print(wait_for(path, pid, "am_on_destroy_called", 20), flush=True)
                    if shell("pidof", PACKAGE).strip() != pid:
                        raise RuntimeError("Portal process exited during normal teardown")
                    prior = pid
                finally:
                    reader.terminate()
                    reader.wait(timeout=10)
        print(f"PASS normal XR teardown {cycle + 1}, PID {prior}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial")
    run(parser.parse_args().serial)
