# SerialCommandCoordinator

## Origin
I was working on a control system prototype and wanted a more interacve way to jump between diagnostic modes when working with an Arduino board, and its connected peripherals, than maintaining multiple sketch versions and flashing the board each time I needed to run it in a different mode. The solution had to have a compact enough memory footprint to be useful on the ATmega328P, yet verbose enough that someone could connect to it via a serial port to run these diagnostics. I couldn't find an open source library I felt was both efficient and simple enough so I wrote my own.

## How it Works
Given an initialized serial stream in the Arduino ecosystem that inherits from Stream (e.g. HardwareSerial or SoftwareSerial), the SerialCommandCoordinator maps a registered serial command, in the form of null terminated character array, received from said stream to functions via a function's memory address. These values are stored and accessed via two, parallel, unsorted arrays allocated on the heap. Since the number of commands will likely be small, performing a search of an unsorted array for mode switching is sufficient as memory footprint is valued over latency.


## How to Use It
### Main Workflow
The main workflow for using the SerialCommandCoordinator is as follows:
1. Initialize the serial stream and SerialCommandCoordinator object
2. Register a command with function
3. Wait for input and run the command if matching input arrives
``` C++

SerialCommandCoordinator scc(Serial);

void performLampTest() {
  // code to turn on and off lamp
}

void scaleTest() {
  // code to check scale values
}

void setup() {
  Serial.begin(9600);
  scc.registerCommand("lampTest", &performLampTest);
  scc.registerCommand("scaleTest", &performScaleTest);
}

void loop {
  scc.update();
}
```
Now, (e.g. using the Arduino IDE and Serial Monitor), when you enter the text **lampTest** in the Serial Monitor, the function ```performLampTest()``` will execute each time the member function ```runSelectedCommand()``` is called. In the example above this will only occur once each time the registered command **lampTest** is entered.

### Setting an Exit Case
There may be instances where it would be advantageous to return to the **receiveCommandInput** loop defined in the previous section. This is just one example of how to exit an operation back to a SerialCommandCoordinator loop, or even exit a SerialCommandCoordinator mode:
``` C++
bool inDiagnosticMode = true;

void scaleTest() {
  bool exec = true;
  while (exec) {
    printFormattedScaleValues();

    if (Serial.available()) {
      char temp = Serial.read();
      if(temp == 'q') {
        clearSerialBuffer();
        exec = false;
      } 
    }
  }
}

void diagnosticMode() {
  SerialCommandCoordinator scc(Serial);
  scc.registerCommand("scaleTest", &scaleTest);
  scc.registerCommand("buttonTest", &buttonTest);
  scc.printCommandList();

  while (inDiagnosticMode) {
    if (scc.receiveCommandInput()) {
      scc.runSelectedCommand();
      scc.printCommandList();
    }
  }

}
```
Note: There are plans to internalize this inside the class to allow for more flexability in use case.

### Considerations
#### Initialization and Destruction
There is not a way to remove or change registered commands as the intent is to initialize once and use either throughout the duration of a program, or until the object is destroyed. This design choice was made in an attempt to reduce the chance of memory fragmentation on the heap, as well as keep the memory footprint of the SerialCommandCoordinator object small.

Note: There are plans to change the allocation of the buffers containing only references to non-managed memory to be allocated on the stack instead of the heap in additional overloaded constructors.

#### Baud Rate
The initial baud rate is set to 9600. If set different in Serial.begin(), the baud rate should also be set for the SerialCommandCoordinator using ```setBaudRate()```. This is to prevent a race condition where the entirety of the serial buffer is read such that the stream.available() shows no new data is present, but no ending character is reached. This can cause data that fills the serial buffer to treated as new input again when it is, instead, intended to be part of the previous input string. The delay equates for the minimum theoretical delay it should take to fill the Arduino's predefined 64 byte serial buffer based on the baud rate, between reads of said buffer.

Note: There are plans to add this to an overload of the class constructor.

#### Limit on Commands
The command list size is currently set to 8 commands. The functions referenced do not support parameters and should be of void return type.

Note: There are plans to add a command list size to an overload of the class constructor. 
 
 ### Public Members
 This is a quick reference. Detailed descriptions of these members can be found in the SerialCommandCoordinator.h file
 ``` C++
SerialCommandCoordinator(Stream &device);
SerialCommandCoordinator(Stream *device);
virtual ~SerialCommandCoordinator();
bool receiveInput();
bool receiveCommandInput();
bool registerCommand(const char *command, const void (*function)(void));
void runSelectedCommand();
void printCommandList();
void printInputBuffer();
void setBaudRate(long baudRate);
const char* getSerialBuffer();
void testStream();  
 ```
