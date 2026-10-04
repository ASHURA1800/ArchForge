#!/usr/bin/env python3
"""
ArchForge OS Network Host Helper
Provides dummy services for the guest OS to interact with during testing.
- UDP echo on :5556
- TCP echo on :5557
- TCP sink (CRC32s everything, prints to build/host_sink.log)
- TCP source (sends N bytes of deterministic pattern)
- HTTP server on :8080 serving a known file
"""

import socket
import threading
import sys
import os
import time
import zlib

BUILD_DIR = os.path.join(os.path.dirname(__file__), "..", "build")
os.makedirs(BUILD_DIR, exist_ok=True)

def udp_echo():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', 5556))
    print("[HOST] UDP echo server listening on :5556")
    while True:
        data, addr = sock.recvfrom(4096)
        sock.sendto(data, addr)

def tcp_echo():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 5557))
    sock.listen(5)
    print("[HOST] TCP echo server listening on :5557")
    while True:
        conn, addr = sock.accept()
        print(f"[HOST] TCP echo: connected from {addr}")
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                conn.sendall(data)
        except Exception as e:
            print(f"[HOST] TCP echo error: {e}")
        finally:
            conn.close()

def tcp_sink():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 5558))
    sock.listen(5)
    print("[HOST] TCP sink server listening on :5558")
    while True:
        conn, addr = sock.accept()
        print(f"[HOST] TCP sink: connected from {addr}")
        crc = zlib.crc32(b"")
        total_bytes = 0
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                crc = zlib.crc32(data, crc)
                total_bytes += len(data)
        except Exception as e:
            print(f"[HOST] TCP sink error: {e}")
        finally:
            with open(os.path.join(BUILD_DIR, "host_sink.log"), "a") as f:
                f.write(f"Received {total_bytes} bytes, CRC32: {crc:08x}\n")
            print(f"[HOST] TCP sink: finished, {total_bytes} bytes, CRC32: {crc:08x}")
            conn.close()

def tcp_source():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 5559))
    sock.listen(5)
    print("[HOST] TCP source server listening on :5559")
    while True:
        conn, addr = sock.accept()
        print(f"[HOST] TCP source: connected from {addr}")
        try:
            # Send 1024 bytes of deterministic pattern
            pattern = bytes([i % 256 for i in range(1024)])
            conn.sendall(pattern)
        except Exception as e:
            print(f"[HOST] TCP source error: {e}")
        finally:
            conn.close()

def http_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', 8080))
    sock.listen(5)
    print("[HOST] HTTP server listening on :8080")
    
    # Create a test file
    test_file = os.path.join(BUILD_DIR, "test.bin")
    with open(test_file, "wb") as f:
        f.write(bytes([i % 256 for i in range(2048)]))
    
    with open(test_file, "rb") as f:
        file_content = f.read()
    
    while True:
        conn, addr = sock.accept()
        print(f"[HOST] HTTP: connected from {addr}")
        try:
            request = conn.recv(1024).decode('utf-8', errors='ignore')
            if "GET /test.bin" in request:
                response = (
                    "HTTP/1.0 200 OK\r\n"
                    f"Content-Length: {len(file_content)}\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "\r\n"
                ).encode('utf-8') + file_content
                conn.sendall(response)
        except Exception as e:
            print(f"[HOST] HTTP error: {e}")
        finally:
            conn.close()

if __name__ == "__main__":
    print("[HOST] Starting network helper services...")
    threads = [
        threading.Thread(target=udp_echo, daemon=True),
        threading.Thread(target=tcp_echo, daemon=True),
        threading.Thread(target=tcp_sink, daemon=True),
        threading.Thread(target=tcp_source, daemon=True),
        threading.Thread(target=http_server, daemon=True),
    ]
    for t in threads:
        t.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("[HOST] Shutting down network helper services...")
        sys.exit(0)