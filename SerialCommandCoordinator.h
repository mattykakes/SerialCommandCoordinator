/**
 *
 * SerialCommandCoordinator library for Arduino
 * v0.0.1
 * https://github.com/mattrussmill/SerialCommandCoordinator
 *
 * MIT License
 * (c) 2023 Matthew Miller
 *
**/
#ifndef SERIALCOMMANDCOORDINATOR_h
#define SERIALCOMMANDCOORDINATOR_h

#include "Arduino.h"

class SerialCommandCoordinator
{
  public:

    SerialCommandCoordinator(Stream &device);
    SerialCommandCoordinator(Stream *device);
    virtual ~SerialCommandCoordinator();

    // Checks the serial stream for available data without blocking. 
    // If bytes are present, they are appended to _inputBuffer at _bufferIndex.
    // Returns true only when the _endMarker is detected (completing a command) 
    // or the buffer overflows. Returns false if the command is still incomplete 
    // or no data is available, allowing the main loop to continue.
    bool receiveInput();

    // First calls receiveInput. If receiveInput is successful, _inputBuffer
    // contains a valid string, attempts to set the selected command with
    // setSelectedFunction(). If successful, returns true and the selected
    // command can be run via runSelectedCommand(). If the command is not
    // recognized, no command will be pre-selected and returns false.
    bool receiveCommandInput();

    // Given a null terminated string and function address, attempts to register
    // a command with its intended routine. It fails and returns false if the 
    // command is already in the list, the list is full, nullptr is an argument,
    // or allocation of memory to store command fails. Returns true on success.
    bool registerCommand(const char *command, const void (*function)(void));

    // Checks for new serial data and executes a matching command if a full line
    // is recieved.
    void update();

    // If the last call to receiveCommandInput() is successful, will run the most
    // recently selected function matching a valid command from the _commandList.
    void runSelectedCommand();

    // Prints all commands currently registered in the _commandList.
    void printCommandList();

    // Prints the current value stored in the _inputBuffer.
    void printInputBuffer();

    // Returns a pointer to the _inputBuffer for use outside of the class.
    const char* getSerialBuffer();
    
     // Stream test function. Prints a single line to the stream reference for testing purposes.
    void testStream();               

  private:
    typedef void(*func_ptr_t)(void);  // Type definition for function pointer (for readability).

    // Initializing code to be shared between all constructors
    void init();

    // Sets the function to be selected
    bool setSelectedFunction();

    Stream *_device = nullptr;        // Address to input stream.
    uint8_t _bufferIndex = 0;         // Current position in _inputBuffer; persists between calls for non-blocking reads.
    char _endMarker = '\n';           // Designated end marker for input stream.

    bool _inputValid = false;         // State of input buffer fitting entirely within the _inputBuffer.
    uint8_t _inputBufferSize = 32;    // Size of the _inputBuffer to be allocated.
    char *_inputBuffer = nullptr;     // Input buffer address for stream input.

    uint8_t _commandListSize = 8;           // Shared index for commands and functions.
    char **_commandList = nullptr;          // List of addresses for registered commands.
    func_ptr_t *_functionList = nullptr;    // List of addresses for registered functions.
    func_ptr_t _functionSelected = nullptr; // Selected function to be run with runSelectedCommand().
};

#endif /* SERIALCOMMANDCOORDINATOR_h */
