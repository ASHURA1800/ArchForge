#!/usr/bin/env python3
"""
QEMU Control Script for ArchForge OS Graphics Testing.
Connects to QEMU monitor socket and sends commands.
"""
import socket
import sys
import time
import os

SOCKET_PATH = "/home/sagar/Projects/ArchForge/build/mon.sock"

def send_command(cmd):
    """Send a command to the QEMU monitor socket."""
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCKET_PATH)
        sock.sendall((cmd + "\n").encode('utf-8'))
        
        response = b""
        sock.settimeout(2.0)
        while True:
            try:
                data = sock.recv(4096)
                if not data:
                    break
                response += data
            except socket.timeout:
                break
        sock.close()
        return response.decode('utf-8', errors='ignore')
    except Exception as e:
        print(f"Error connecting to QEMU monitor: {e}", file=sys.stderr)
        return None

def wait_for_marker(marker, timeout=30):
    """Wait for a specific marker in the serial log."""
    serial_log = "/home/sagar/Projects/ArchForge/build/serial.log"
    start_time = time.time()
    while time.time() - start_time < timeout:
        if os.path.exists(serial_log):
            with open(serial_log, 'r') as f:
                if marker in f.read():
                    return True
        time.sleep(0.1)
    print(f"Timeout waiting for marker: {marker}", file=sys.stderr)
    return False

def main():
    if len(sys.argv) < 2:
        print("Usage: qemu_ctl.py <command> [args...]")
        print("Commands: screendump <file>, sendkey <key>, mouse_move <dx> <dy>, mouse_button <mask>, system_reset, quit, wait <marker>")
        sys.exit(1)
    
    cmd = sys.argv[1]
    
    if cmd == "wait":
        if len(sys.argv) < 3:
            print("Usage: qemu_ctl.py wait <marker>")
            sys.exit(1)
        marker = sys.argv[2]
        if wait_for_marker(marker):
            print(f"Marker found: {marker}")
            sys.exit(0)
        else:
            sys.exit(1)
            
    elif cmd == "screendump":
        if len(sys.argv) < 3:
            print("Usage: qemu_ctl.py screendump <file>")
            sys.exit(1)
        filename = sys.argv[2]
        # Ensure directory exists
        os.makedirs(os.path.dirname(filename) or ".", exist_ok=True)
        resp = send_command(f"screendump {filename}")
        print(f"Screendump response: {resp.strip()}")
        
    elif cmd == "sendkey":
        if len(sys.argv) < 3:
            print("Usage: qemu_ctl.py sendkey <key>")
            sys.exit(1)
        key = sys.argv[2]
        resp = send_command(f"sendkey {key}")
        print(f"Sendkey response: {resp.strip()}")
        
    elif cmd == "mouse_move":
        if len(sys.argv) < 4:
            print("Usage: qemu_ctl.py mouse_move <dx> <dy>")
            sys.exit(1)
        dx = sys.argv[2]
        dy = sys.argv[3]
        resp = send_command(f"mouse_move {dx} {dy}")
        print(f"Mouse move response: {resp.strip()}")
        
    elif cmd == "mouse_button":
        if len(sys.argv) < 3:
            print("Usage: qemu_ctl.py mouse_button <mask>")
            sys.exit(1)
        mask = sys.argv[2]
        resp = send_command(f"mouse_button {mask}")
        print(f"Mouse button response: {resp.strip()}")
        
    elif cmd == "system_reset":
        resp = send_command("system_reset")
        print(f"System reset response: {resp.strip()}")
        
    elif cmd == "quit":
        resp = send_command("quit")
        print(f"Quit response: {resp.strip()}")
        
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)

if __name__ == "__main__":
    main()
