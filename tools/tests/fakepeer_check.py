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


def check_close(build, welcome):
    # Inspect HELLO and close the session before or after a welcome.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(('127.0.0.1', 0))
        server.settimeout(3)
        args = [sys.executable, '-I', str(PEER), f'127.0.0.1:{server.getsockname()[1]}',
                '--seconds', '2']
        if build is not None:
            args += ['--build', build]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            data, addr = server.recvfrom(2048)
            header = '<IHIHHQQB'
            magic, proto, conn, seq, _, _, _, _ = struct.unpack_from(header, data)
            pos = struct.calcsize(header)
            typ, flags, _, _ = struct.unpack_from('<BBHH', data, pos)
            hello_proto, hello_build, _ = struct.unpack_from('<HIB', data, pos + 6)
            expected = int(build or 'dc04aee5', 16)
            if (typ, flags, hello_proto, hello_build) != (1, 1, proto, expected):
                raise AssertionError(f'unexpected HELLO: {typ, flags, hello_proto, hello_build}')
            messages = b''
            if welcome:
                payload = struct.pack('<BIB', 1, 0x12345678, 0)
                messages += struct.pack('<BBH', 2, 0, len(payload)) + payload
            messages += struct.pack('<BBH', 10, 0, 1) + b'\0'
            server.sendto(struct.pack(header, magic, proto, conn, 0, seq, 0, 0,
                                      2 if welcome else 1) + messages, addr)
            out, err = proc.communicate(timeout=3)
            if proc.returncode != (0 if welcome else 1) or 'host closed the session' not in out:
                raise AssertionError(f'host close: {proc.returncode} {out} {err}')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()


def check_session(host_option, build, wrong_build):
    host = subprocess.Popen([str(HOST), host_option], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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

        # A different protocol must not be accepted as a real guest.
        spec = importlib.util.spec_from_file_location("fakepeer", PEER)
        peer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(peer)
        current_proto = peer.PROTO
        for proto in (current_proto - 1, current_proto + 1):
            peer.PROTO = proto
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(0.25)
                conn = peer.Conn(sock, ("127.0.0.1", port))
                conn.send_reliable(peer.HELLO, struct.pack('<HIB', proto, 0xdc04aee5, 3) + b'Bot')
                conn.flush(time.time())
                try:
                    sock.recvfrom(2048)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"host answered incompatible protocol {proto}")

        result = subprocess.run(
            [sys.executable, '-I', str(PEER), f'127.0.0.1:{port}', '--build', wrong_build,
             '--seconds', '3'], capture_output=True, text=True, timeout=5,
        )
        if result.returncode == 0 or 'rejected: different SporeApp.exe build' not in result.stdout:
            raise AssertionError(f'wrong build: {result.returncode} {result.stdout} {result.stderr}')

        args = [sys.executable, '-I', str(PEER), f'127.0.0.1:{port}', '--seconds', '5',
                '--say', 'fakepeer test']
        if build is not None:
            args += ['--build', build]
        result = subprocess.run(
            args,
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode or "welcome: my id 1" not in result.stdout:
            raise AssertionError(f"fakepeer failed ({result.returncode}): {result.stdout} {result.stderr}")
        _, error = host.communicate(timeout=10)
        if host.returncode:
            raise AssertionError(f"host did not receive the expected messages: {error}")
        print(f"fakepeer: {host_option} handshake, player state, chat and leave OK")
    finally:
        if host.poll() is None:
            host.kill()
            host.communicate()


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

    check_close(None, False)
    check_close('922af7c7', True)
    check_session('--fakepeer-host', None, '922af7c7')
    check_session('--fakepeer-host-gog2016', '922af7c7', 'dc04aee5')


if __name__ == "__main__":
    main()
