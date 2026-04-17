# SerialCommandCoordinator

## How it Works
The **SerialCommandCoordinator** maps serial string commands to function addresses without using the heap, making it ideal for memory-constrained environments like the ATmega328P. 

Unlike previous versions that relied on dynamic memory, this revamped library uses **C++ Templates** to allocate command lists and buffers statically at compile-time. Command strings are stored directly in **Program Memory (Flash)** using the `F()` macro, ensuring that SRAM is preserved for your application logic. The library operates using a non-blocking state machine, allowing your main loop to continue running while serial data is being gathered.

## How to Use It
### Main Workflow
1. **Initialize**: Declare the object as a template. You can specify the maximum number of commands and the buffer size, or use the optimized defaults.
2. **Register**: Map string literals (in Flash) to `void` functions.
3. **Update**: Call `update()` in your main loop to automatically handle input and execution.

```cpp
#include <SerialCommandCoordinator.h>

// Initialize with defaults: 8 commands, 32-byte buffer
SerialCommandCoordinator<> scc(Serial);

void performLampTest() {
  // code to turn on and off lamp
}

void scaleTest() {
  // code to check scale values
}

void setup() {
  Serial.begin(9600);
  
  // Register commands using the F() macro to save SRAM
  scc.registerCommand(F("lampTest"), &performLampTest);
  scc.registerCommand(F("scaleTest"), &scaleTest);
}

void loop() {
  // Non-blocking update: checks serial and runs commands automatically
  scc.update();
  
  // Your other application code runs freely here
}
```

### Breaking Out of Loops
When a registered function initiates a persistent execution loop (e.g., a continuous sensor polling routine), it occupies the processor's execution context, preventing the primary `SerialCommandCoordinator::update()` cycle from monitoring the serial stream. To maintain interactivity without exiting the local scope, the `checkForBreak()` method enables the function to perform a non-blocking poll of the serial buffer for a specific termination signal.

```
void scaleTest() {
  Serial.println(F("Scale Test active. Press 'q' to stop."));
  
  while (true) {
    // Perform diagnostic work
    printScaleData();

    // Check for the break character (default is 'q')
    if (scc.checkForBreak()) {
      Serial.println(F("Exiting Test..."));
      return; // Jumps back to scc.update() in the main loop
    }
  }
}
```

### Customizing the Break Signal
You can change the character used to trigger a break at any time.
```
void setup() {
  Serial.begin(9600);
  scc.setBreakChar('x'); // Change break character to 'x'
  scc.registerCommand(F("scaleTest"), &scaleTest);
}
```

### Advanced Initialization
Because this is a template-based library, you can customize the memory footprint based on your specific hardware needs without editing the library source:
```
// Custom sizing: support for 12 commands and a larger 64-byte buffer
SerialCommandCoordinator<12, 64> scc(Serial);
```

## Considerations
### Zero-Heap Allocation
This library has been re-engineered to eliminate malloc, free, and calloc. All arrays are fixed-size and allocated in the Static Data section of the RAM. This prevents runtime crashes due to heap fragmentation and allows the compiler to provide accurate memory usage reports during the build process.

### Non-Blocking & Overflow Protection
The library no longer uses delay() or timing-based reads. It features a Discarding State: if an incoming command exceeds the defined BUFFER_SIZE, the utility enters a non-blocking "ignore" mode until the next newline is reached. This protects the system from processing "garbage" data without halting your program.

### Flash Memory (PROGMEM)
To maximize SRAM efficiency on 8-bit AVR boards, all registered command strings are stored in Flash. This is why the F() macro is required during registration. On 32-bit boards (ESP32, ARM), the library automatically aliases to compatible types, maintaining a unified codebase.