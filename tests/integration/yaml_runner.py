import yaml
import socket
import time
import sys
import argparse

def run_scenario(yaml_path, port):
    # 1. Load the Wokwi Scenario
    try:
        with open(yaml_path, 'r') as f:
            scenario = yaml.safe_load(f)
    except Exception as e:
        print(f"Error loading YAML: {e}")
        sys.exit(1)

    print(f"--- Executing Scenario: {scenario.get('name', 'Unknown')} ---")

    # 2. Connect to the Emulator's TCP Socket
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Set a strict 10-second timeout per step to prevent hung CI jobs
    s.settimeout(10.0) 
    
    try:
        s.connect(('127.0.0.1', port))
        print(f"Connected to emulator on port {port}")
    except Exception as e:
        print(f"Failed to connect to emulator: {e}")
        sys.exit(1)

    # Give the virtual bootloader a moment to settle
    time.sleep(1)

    buffer = ""
    
    # 3. Parse and Execute the YAML Steps
    for step in scenario.get('steps', []):
        
        # --- COMMAND: wait-serial ---
        if 'wait-serial' in step:
            target = step['wait-serial']
            print(f"Waiting for: '{target}'...")
            while target not in buffer:
                try:
                    data = s.recv(1024).decode('utf-8', errors='ignore')
                    if not data:
                        break
                    buffer += data
                except socket.timeout:
                    print(f"\n[TIMEOUT] Expected '{target}' but buffer contained: {repr(buffer)}")
                    sys.exit(1)
            
            print(f"  -> Found '{target}'")
            # Clear buffer up to the target to prevent false positives on the next wait
            buffer = buffer[buffer.find(target) + len(target):]

        # --- COMMAND: write-serial ---
        elif 'write-serial' in step:
            payload = step['write-serial']
            # Decode escaped characters like \n from the YAML string
            payload = payload.encode('utf-8').decode('unicode_escape')
            print(f"Writing: {repr(payload)}")
            s.sendall(payload.encode('utf-8'))

        # --- COMMAND: delay ---
        elif 'delay' in step:
            delay_str = str(step['delay'])
            print(f"Delaying for {delay_str}...")
            if delay_str.endswith('ms'):
                time.sleep(int(delay_str[:-2]) / 1000.0)
            elif delay_str.endswith('s'):
                time.sleep(float(delay_str[:-1]))
            else:
                time.sleep(float(delay_str))

    print("--- Scenario Completed Successfully! ---")
    s.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--yaml', required=True, help="Path to universal_test.yaml")
    parser.add_argument('--port', type=int, required=True, help="TCP port of the emulator")
    args = parser.parse_args()

    run_scenario(args.yaml, args.port)