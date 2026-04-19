import yaml
import subprocess
import time
import sys
import argparse
import os
import fcntl

def run_scenario(yaml_path, binary_path):
    try:
        with open(yaml_path, 'r') as f:
            scenario = yaml.safe_load(f)
    except Exception as e:
        print(f"Error loading YAML: {e}")
        sys.exit(1)

    print(f"--- Executing Scenario: {scenario.get('name', 'Unknown')} ---")

    # Spawn the native Linux executable
    process = subprocess.Popen(
        [binary_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1 # Line-buffered
    )

    # Make stdout non-blocking so our timeout logic works exactly like sockets
    flags = fcntl.fcntl(process.stdout, fcntl.F_GETFL)
    fcntl.fcntl(process.stdout, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    buffer = ""
    
    for step in scenario.get('steps', []):
        
        # --- COMMAND: wait-serial ---
        if 'wait-serial' in step:
            target = step['wait-serial']
            print(f"Waiting for: '{target}'...")
            
            start_time = time.time()
            while target not in buffer:
                if time.time() - start_time > 10.0:
                    print(f"\n[TIMEOUT] Expected '{target}' but buffer contained: {repr(buffer)}")
                    process.kill()
                    sys.exit(1)
                
                try:
                    data = process.stdout.read()
                    if data:
                        buffer += data
                except TypeError:
                    pass # Non-blocking read returns None if empty
                
                time.sleep(0.01) # Yield to CPU
            
            print(f"  -> Found '{target}'")
            buffer = buffer[buffer.find(target) + len(target):]

        # --- COMMAND: write-serial ---
        elif 'write-serial' in step:
            payload = step['write-serial']
            payload = payload.encode('utf-8').decode('unicode_escape')
            print(f"Writing: {repr(payload)}")
            process.stdin.write(payload)
            process.stdin.flush()

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
    process.terminate()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--yaml', required=True, help="Path to universal_test.yaml")
    parser.add_argument('--binary', required=True, help="Path to the compiled executable")
    args = parser.parse_args()

    run_scenario(args.yaml, args.binary)