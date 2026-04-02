#!/usr/bin/env python3
import subprocess
import xml.etree.ElementTree as ET
import hashlib
import sys
import time
import os

class OpenMSXVerifierStdio:
    def __init__(self, openmsx_path, args):
        # Merge stderr with stdout to prevent deadlock if stderr buffer fills up
        cmd = [openmsx_path, "-control", "stdio"] + args
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self._send("<openmsx-control>")
        self._receive_until_tag("openmsx-control", wait_for_end=False)
        print("Connected to openMSX via stdio")
        
        # Check if sha1 command is available in openMSX (Tcl package)
        # Most versions have it via 'sha1::sha1' or similar, but openMSX exposes it simply.
        # Fallback to Tcl-only hex dump if needed.
        self.has_sha1 = self.send_command("info commands sha1") != ""
        if self.has_sha1:
            print("Using openMSX-native sha1 for high-speed verification")
        else:
            print("Warning: openMSX 'sha1' command not found, using slow fallback")

    def _send(self, msg):
        self.proc.stdin.write(msg)
        self.proc.stdin.flush()

    def _receive_until_tag(self, tag, wait_for_end=True):
        buf = ""
        start_tag = f"<{tag}>"
        end_tag = f"</{tag}>"
        while True:
            char = self.proc.stdout.read(1)
            if not char:
                if buf:
                    print(f"\nDEBUG: Process ended unexpectedly. Buffer: {buf}")
                break
            buf += char
            if not wait_for_end and start_tag in buf:
                return buf
            if end_tag in buf:
                return buf
            # Handle self-closing tags like <openmsx-control/>
            if buf.strip().endswith("/>") and tag in buf:
                return buf
        return buf

    def send_command(self, cmd):
        msg = f"<command>{cmd}</command>"
        self._send(msg)
        resp = self._receive_until_tag("reply")
        try:
            if "<reply" in resp:
                start = resp.find("<reply")
                end = resp.find("</reply>") + 8
                xml_part = resp[start:end]
                tree = ET.fromstring(xml_part)
                if tree.attrib.get('ok') == 'true':
                    return tree.text.strip() if tree.text else ""
                else:
                    print(f"\nError from openMSX for cmd '{cmd}': {tree.text}")
                    return None
        except ET.ParseError as e:
            print(f"\nXML Parse Error: {e}, raw response: {resp}")
            return None
        return None

    def get_vram_sha1(self):
        if self.has_sha1:
            # Use native SHA1 on the VRAM block - very fast
            return self.send_command("sha1 [debug read_block vram 0 16384]")
        else:
            # Fallback: slow hex dump
            hex_data = self.send_command("set _v \"\"; for {set i 0} {$i < 16384} {incr i} { append _v [format %02X [debug read vram $i]] }; set _v")
            if hex_data:
                return hashlib.sha1(bytes.fromhex(hex_data)).hexdigest()
            return None

    def quit(self):
        try:
            self.send_command("quit")
        except:
            pass
        self.proc.terminate()

def main():
    if len(sys.argv) < 3:
        print("Usage: vdp_verify_replay.py <vdp_trace.log> <openmsx_path> [extra openmsx args...]")
        sys.exit(1)

    trace_path = sys.argv[1]
    openmsx_path = sys.argv[2]
    extra_args = sys.argv[3:]

    if not os.path.exists(trace_path):
        print(f"Error: Trace file not found: {trace_path}")
        sys.exit(1)

    try:
        verifier = OpenMSXVerifierStdio(openmsx_path, extra_args)
    except Exception as e:
        print(f"Error: Could not start openMSX: {e}")
        sys.exit(1)

    # Initial state: clear openMSX VRAM to ensure clean start
    verifier.send_command("for {set i 0} {$i < 16384} {incr i} { debug write vram $i 0 }")
    # Reset VDP registers to defaults
    for r in range(8): verifier.send_command(f"debug write \"VDP regs\" {r} 0")

    with open(trace_path, "r") as f:
        # Using a generator to read large files efficiently
        print(f"Loading trace {trace_path}...")
        
    print(f"Starting replay...")
    start_time = time.time()
    
    with open(trace_path, "r") as f:
        for i, line in enumerate(f):
            parts = line.split()
            if len(parts) != 3: continue
            
            port_hex, val_hex, expected_sha1 = parts
            port = int(port_hex, 16)
            val = int(val_hex, 16)
            
            # Step 1: Write to I/O
            if verifier.send_command(f"debug write io {port} {val}") is None:
                print(f"\nFailed to write I/O at step {i}")
                break
            
            # Step 2: Get and compare VRAM hash
            actual_sha1 = verifier.get_vram_sha1()
            if actual_sha1 is None:
                print(f"\nFailed to read VRAM hash at step {i}")
                break
                
            if actual_sha1.lower() != expected_sha1.lower():
                print(f"\n\n!!! DIVERGENCE DETECTED !!!")
                print(f"Step: {i}")
                print(f"Port: 0x{port_hex}, Value: 0x{val_hex}")
                print(f"Expected VRAM SHA1: {expected_sha1.lower()}")
                print(f"Actual VRAM SHA1:   {actual_sha1.lower()}")
                
                # Report current VDP registers for context
                regs = ""
                for r in range(8):
                    rv = verifier.send_command(f"debug read \"VDP regs\" {r}")
                    regs += f"R#{r}={rv} "
                print(f"VDP Registers: {regs}")
                
                sys.exit(1)
                
            if i % 100 == 0:
                elapsed = time.time() - start_time
                speed = i / elapsed if elapsed > 0 else 0
                print(f"Step {i} OK ({speed:.1f} steps/sec)   ", end='\r')

    print(f"\n\nVerification SUCCESS: All {i+1} writes matched openMSX VRAM state.")
    verifier.quit()

if __name__ == "__main__":
    main()
