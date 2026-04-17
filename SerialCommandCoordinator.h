/**
 * @file SerialCommandCoordinator.h
 * @author Matthew Miller
 * @brief A memory-efficient, non-blocking serial command dispatcher for Arduino.
 * @version 0.1.0
 * * MIT License
 * (c) 2023-2026 Matthew Miller
**/

#ifndef SERIALCOMMANDCOORDINATOR_h
#define SERIALCOMMANDCOORDINATOR_h

#ifndef SERIAL_RX_BUFFER_SIZE
  #define SERIAL_RX_BUFFER_SIZE 64
#endif

#include "Arduino.h"

#if defined(__AVR__)
  #include <avr/pgmspace.h>
#else
  /** * @brief Unified memory (32-bit) compatibility layer.
   * 'const' ensures the compiler keeps data in Flash to save SRAM. 
   * Macros alias AVR Flash-functions to standard C for portability.
   */
  #define PGM_P const char*
  #define strcmp_P strcmp
#endif

/**
 * @class SerialCommandCoordinator
 * @brief Maps serial string inputs to function addresses without using the heap.
 * * @tparam MAX_COMMANDS Maximum number of commands that can be registered.
 * @tparam BUFFER_SIZE Size of the internal RX buffer (defaults to half of hardware buffer).
 */
template<size_t MAX_COMMANDS = 8, uint8_t BUFFER_SIZE = (SERIAL_RX_BUFFER_SIZE / 2)>
class SerialCommandCoordinator
{
  public:
    /** @brief Construct using a reference to a Stream (e.g., Serial). */
    SerialCommandCoordinator(Stream &device) : _device(&device) {}
    
    /** @brief Construct using a pointer to a Stream. */
    SerialCommandCoordinator(Stream *device) : _device(device) {}
    
    virtual ~SerialCommandCoordinator() {}

    /**
     * @brief Given a null terminated string and function address, attempts to register 
     * a command with its intended routine. 
     * * @param command A string literal wrapped in the F() macro.
     * @param function Pointer to a void function with no parameters.
     * @return Fails and returns false if the command is already in the list, 
     * the list is full, or nullptr is an argument. Returns true on success.
     */
    bool registerCommand(const __FlashStringHelper *command, const void (*function)(void)) {
      if (command == nullptr || function == nullptr) {
        return false;
      }
      
      // find next empty spot in list
      int ndx = 0;
      while (ndx < MAX_COMMANDS) {
        if (_commandList[ndx] == nullptr) break;

        // command already in list - architecture-aware comparison
        if (strcmp_P((const char*)command, (PGM_P)_commandList[ndx]) == 0) return false;
        ndx++;
      }

      // Command buffer full, cannot register command
      if (ndx >= MAX_COMMANDS) return false; 

      _commandList[ndx] = command;
      _functionList[ndx] = (func_ptr_t)function;
      return true;
    }

    /**
     * @brief The primary non-blocking execution entry point.
     * * Checks for new serial data and executes a matching command if a full line 
     * (ending in _endMarker) is received. Should be called once per loop().
     */
    void update() {
      if (receiveCommandInput()) {
        runSelectedCommand();
      }
    }

    /**
     * @brief If the last call to receiveCommandInput() is successful, will run the 
     * most recently selected function matching a valid command from the _commandList.
     */
    void runSelectedCommand() {
      if (!_inputValid || _functionSelected == nullptr) {
        return;
      }
      (*_functionSelected)();
    }

    /** @brief Prints all commands currently registered in the _commandList. */
    void printCommandList() {
      for (int i = 0; i < MAX_COMMANDS; i++) {
        if (_commandList[i] != nullptr) {
          _device->println(_commandList[i]);
        }
      }
    }

    /**
     * @brief Checks the serial stream for the designated break character to exit a local loop.
     * * This allows a function that is executing its own internal loop to poll the stream 
     * for a termination signal. It uses peek() to check the next available character 
     * without consuming it unless it matches the _breakChar.
     * * @return true if the break character was detected and consumed.
     */
    bool checkForBreak() {
      if (_device->available() > 0) {
        if (_device->peek() == _breakChar) {
          _device->read(); // Consume the break character
          return true; 
        }
      }
      return false;
    }

    /**
     * @brief Sets the character used to signal an abort or break from a loop.
     * @param c The character to look for (default is often 'q' or ESC).
     */
    void setBreakChar(char c) {
      _breakChar = c;
    }

    /** @brief Prints the current value stored in the _inputBuffer. */
    void printInputBuffer() {
      _device->println(_inputBuffer);
    }

    /** @brief Returns a pointer to the _inputBuffer for use outside of the class. */
    const char* getSerialBuffer() { return _inputBuffer; }

  private:
    typedef void(*func_ptr_t)(void);

    /**
     * @brief Checks the serial stream for available data without blocking. 
     * * If bytes are present, they are appended to _inputBuffer at _bufferIndex.
     * Returns true only when the _endMarker is detected (completing a command) 
     * or the buffer overflows. Returns false if the command is still incomplete 
     * or no data is available, allowing the main loop to continue.
     */
    bool receiveInput() {
      char rc;

      while (_device->available() > 0) {
        rc = _device->read();

        if (rc != _endMarker) {
          if (_discarding) continue; 

          // check for buffer overflow
          if (_bufferIndex < BUFFER_SIZE - 1) {
            _inputBuffer[_bufferIndex] = rc;
            _bufferIndex++;
          } else {
            // buffer is full: enter discard state to protect next command
            _inputBuffer[_bufferIndex] = '\0'; // terminate the string
            _inputValid = false; // input string too large for buffer
            _discarding = true;
          }
        } else {
          // end marker reached
          bool wasDiscarding = _discarding;
          _inputBuffer[_bufferIndex] = '\0';
          _bufferIndex = 0; // reset index for the next command
          _discarding = false; // Reset state for next command

          if (wasDiscarding) {
            _inputValid = false;
            return false; // Silently drop over-sized command
          }

          _inputValid = true;
          return true;
        }
      }
      return false; // full command not received yet
    }

    /**
     * @brief First calls receiveInput. 
     * * If receiveInput is successful, _inputBuffer contains a valid string, 
     * attempts to set the selected command with setSelectedFunction(). 
     * @return true if successful; the selected command can be run via runSelectedCommand(). 
     * If the command is not recognized, no command will be pre-selected and returns false.
     */
    bool receiveCommandInput() {
      if (receiveInput()) {
        return setSelectedFunction();
      }
      return false;
    }

    /**
     * @brief Sets the function to be selected.
     * * registered functions can't be removed, nullptr is end of list = not found.
     * @return true if a function was found and selected.
     */
    bool setSelectedFunction() {
      int ndx = 0;
      _functionSelected = nullptr;

      while (ndx < MAX_COMMANDS && _inputValid) {
        if (_commandList[ndx] == nullptr) {
          return false;
        }

        // found function - architecture-aware comparison
        if (strcmp_P(_inputBuffer, (PGM_P)_commandList[ndx]) == 0) {
          _functionSelected = _functionList[ndx];
          return true;
        }
        ndx++;    
      }
      return false; // not found in list
    }

    Stream *_device = nullptr;        ///< Address to input stream.
    uint8_t _bufferIndex = 0;         ///< Current position in _inputBuffer; persists between calls for non-blocking reads.
    char _endMarker = '\n';           ///< Designated end marker for input stream.
    char _breakChar = 'q';            ///< Character used to signal an abort from looping functions.
    bool _discarding = false;         ///< State used to ignore characters after an overflow until _endMarker.

    bool _inputValid = false;         ///< State of input buffer fitting entirely within the _inputBuffer.
    char _inputBuffer[BUFFER_SIZE] = {0}; ///< Input buffer address for stream input. Stored statically (Zero-Heap).

    const __FlashStringHelper *_commandList[MAX_COMMANDS] = {nullptr}; ///< List of addresses for registered command strings stored in flash.
    func_ptr_t _functionList[MAX_COMMANDS] = {nullptr};                ///< List of addresses for registered functions.
    func_ptr_t _functionSelected = nullptr;                            ///< Selected function to be run with runSelectedCommand().
};

#endif /* SERIALCOMMANDCOORDINATOR_h */