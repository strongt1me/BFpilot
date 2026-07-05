import socket
import time

import os

try:
    elf_path = os.path.join(os.path.dirname(__file__), "../bfpilot.elf")
    with open(elf_path, "rb") as f:
        payload = f.read()
    print(f"Loaded {len(payload)} bytes from {elf_path}")
    s = socket.socket()
    s.connect(("192.168.1.204", 9021))
    s.sendall(payload)
    s.close()
    print("Injected successfully!")
except Exception as e:
    print(f"Error: {e}")
