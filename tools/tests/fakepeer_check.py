#!/usr/bin/env python3
"""Exercise fakepeer against the actual C++ Session over localhost UDP."""
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / 'build' / 'host' / ('net_tests.exe' if sys.platform == 'win32' else 'net_tests')
PEER = ROOT / 'tools' / 'fakepeer.py'


def check_close_before_welcome(protocol):
    # A host closing before accepting the guest must not look like a successful join.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(('127.0.0.1', 0))
        server.settimeout(3)
        proc = subprocess.Popen([sys.executable, '-I', str(PEER),
                                 f'127.0.0.1:{server.getsockname()[1]}', '--seconds', '2',
                                 '--protocol', str(protocol)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            data, addr = server.recvfrom(2048)
            header = '<IHIHHQQB'
            magic, proto, conn, seq, _, _, _, _ = struct.unpack_from(header, data)
            hello_proto, = struct.unpack_from('<H', data, struct.calcsize(header) + 6)
            if proto != protocol or hello_proto != protocol:
                raise AssertionError(f'protocol mismatch: header={proto} HELLO={hello_proto}')
            server.sendto(struct.pack(header, magic, proto, conn, 0, seq, 0, 0, 1) +
                          struct.pack('<BBH', 10, 0, 1) + b'\0', addr)
            out, err = proc.communicate(timeout=3)
            if proc.returncode == 0 or 'host closed the session' not in out:
                raise AssertionError(f'host close: {proc.returncode} {out} {err}')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()


def check_session():
    host = subprocess.Popen([str(HOST), '--fakepeer-host'], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    try:
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(host.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=5).strip()
        except queue.Empty:
            raise AssertionError('host did not start within 5 seconds') from None
        if not line.isdigit():
            raise AssertionError(f'host failed to start: {line}')
        addr = f'127.0.0.1:{line}'

        wrong = subprocess.run([sys.executable, '-I', str(PEER), addr, '--build', '922af7c7',
                                '--seconds', '3'], capture_output=True, text=True, timeout=5)
        if wrong.returncode == 0 or 'rejected: different SporeApp.exe build' not in wrong.stdout:
            raise AssertionError(f'wrong build: {wrong.returncode} {wrong.stdout} {wrong.stderr}')

        peer = subprocess.run([sys.executable, '-I', str(PEER), addr, '--build', 'dc04aee5',
                               '--seconds', '5', '--say', 'fakepeer test'],
                              capture_output=True, text=True, timeout=10)
        if peer.returncode or 'welcome: my id 1' not in peer.stdout:
            raise AssertionError(f'fakepeer failed: {peer.returncode} {peer.stdout} {peer.stderr}')
        _, error = host.communicate(timeout=10)
        if host.returncode:
            raise AssertionError(f'host did not receive the expected messages: {error}')
        print('fakepeer: handshake, player state, chat, leave and build mismatch OK')
    finally:
        if host.poll() is None:
            host.kill()
            host.communicate()


if __name__ == '__main__':
    check_close_before_welcome(6)
    check_close_before_welcome(7)
    check_session()
