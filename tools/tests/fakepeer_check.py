#!/usr/bin/env python3
"""Exercise fakepeer against the actual C++ Session over localhost UDP."""
import importlib.util
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "build" / "host" / ("net_tests.exe" if sys.platform == "win32" else "net_tests")
PEER = ROOT / "tools" / "fakepeer.py"


def main():
    # Expiring before a welcome is an unsuccessful join, even for short runs.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as silent_host:
        silent_host.bind(('127.0.0.1', 0))
        result = subprocess.run(
            [sys.executable, '-I', str(PEER), f'127.0.0.1:{silent_host.getsockname()[1]}',
             '--seconds', '0.15'], capture_output=True, text=True, timeout=3,
        )
        if result.returncode == 0 or 'no welcome received' not in result.stdout:
            raise AssertionError(f'silent host: {result.returncode} {result.stdout} {result.stderr}')

    host = subprocess.Popen([str(HOST), '--fakepeer-host'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(host.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=5).strip()
        except queue.Empty:
            raise AssertionError("host did not start within 5 seconds") from None
        if not line.isdigit():
            raise AssertionError(f"host failed to start: {line}")
        port = int(line)

        # A future/incompatible protocol must not be accepted as a real guest.
        spec = importlib.util.spec_from_file_location("fakepeer", PEER)
        peer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(peer)
        peer.PROTO += 1
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(0.25)
            conn = peer.Conn(sock, ("127.0.0.1", port))
            conn.send_reliable(peer.HELLO, struct.pack('<HIB', peer.PROTO, 0xdc04aee5, 3) + b'Bot')
            conn.flush(time.time())
            try:
                sock.recvfrom(2048)
            except socket.timeout:
                pass
            else:
                raise AssertionError("host answered an incompatible protocol")

        result = subprocess.run(
            [sys.executable, "-I", str(PEER), f"127.0.0.1:{port}", "--seconds", "5", "--say", "fakepeer test"],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode or "welcome: my id 1" not in result.stdout:
            raise AssertionError(f"fakepeer failed ({result.returncode}): {result.stdout} {result.stderr}")
        _, error = host.communicate(timeout=10)
        if host.returncode:
            raise AssertionError(f"host did not receive the expected messages: {error}")
        print("fakepeer: handshake, player state, chat and leave OK")
    finally:
        if host.poll() is None:
            host.kill()
            host.communicate()


if __name__ == "__main__":
    main()
