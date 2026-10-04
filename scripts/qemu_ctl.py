#!/usr/bin/env python3
import socket
import time
import sys
import os

def send_command(sock, cmd):
    sock.sendall((cmd + "\n").encode('utf-8'))
    resp = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        resp += chunk
        if b"(qemu)" in resp:
            break
    return resp.decode('utf-8', errors='ignore')

def main():
    if len(sys.argv) < 3:
        print("Usage: qemu_ctl.py <socket_path> <command> [args...]")
        sys.exit(1)
    
    sock_path = sys.argv[1]
    cmd = sys.argv[2]
    
    for _ in range(50):
        if os.path.exists(sock_path):
            break
        time.sleep(0.1)
    else:
        print(f"Socket {sock_path} not found")
        sys.exit(1)
        
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(sock_path)
    
    if cmd == "screendump":
        ppm_path = sys.argv[3]
        resp = send_command(sock, f"screendump {ppm_path}")
        print(f"Screendump saved to {ppm_path}")
    elif cmd == "quit":
        send_command(sock, "quit")
    else:
        print(f"Unknown command: {cmd}")
        
    sock.close()

if __name__ == "__main__":
    main()
