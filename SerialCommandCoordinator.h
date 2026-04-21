/**
 * @file SerialCommandCoordinator.h
 * @author Matthew Miller
 * @brief A memory-efficient, non-blocking serial command dispatcher for Arduino.
 * @version 0.1.1
 * * MIT License
 * (c) 2023 Matthew Miller
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
  #ifndef PGM_P
    #define PGM_P const char*
  #endif
  
  #ifndef strcmp_P
    #define strcmp_P strcmp
  #endif

  // Polyfill: Allows the parser to use 8-bit fast-fail optimizations (bypassing 
  // strcmp_P overhead) without penalizing 32-bit memory-mapped architectures.
  #ifndef pgm_read_byte
    #define pgm_read_byte(addr) (*(const unsigned char *)(addr))
  #endif
#endif

/**
 * @class SerialCommandCoordinator
 * @brief Maps serial string inputs to function addresses without using the heap.
 * * @tparam MAX_COMMANDS Maximum number of commands that can be registered.
 * @tparam BUFFER_SIZE Size of the internal RX buffer (defaults to half of hardware buffer).
 */
template<
  size_t MAX_COMMANDS = 8,
  uint8_t BUFFER_SIZE = (SERIAL_RX_BUFFER_SIZE / 2),
  char DEFAULT_BREAK = '!',
  char END_MARKER = '\n'
>
class SerialCommandCoordinator
{
  public:
    /** @brief Construct using a reference to a Stream (e.g., Serial). */
    SerialCommandCoordinator(Stream &device) : _device(&device) {}
    
    /** @brief Construct using a pointer to a Stream. */
    SerialCommandCoordinator(Stream *device) : _device(device) {}
    
    ~SerialCommandCoordinator() {}

    /**
     * @brief Given a null terminated string and function address, attempts to register 
     * a command with its intended routine. 
     * * @param command A string literal wrapped in the F() macro.
     * @param function Pointer to a void function with no parameters.
     * @return Fails and returns false if the command is already in the list, 
     * the list is full, or nullptr is an argument. Returns true on success.
     */
    bool registerCommand(const __FlashStringHelper *command, void (*function)(void)) {      
      if (command == nullptr || function == nullptr) {
        return false;
      }
      
      // find next empty spot in list
      uint8_t ndx = 0;
      while (ndx < MAX_COMMANDS) {
        if (_commands[ndx].commandStr == nullptr) break;

        // command already in list - architecture-aware comparison
        if (strcmp_P((const char*)command, (PGM_P)_commands[ndx].commandStr) == 0) return false;
        ndx++;
      }

      // Command buffer full, cannot register command
      if (ndx >= MAX_COMMANDS) return false; 

      _commands[ndx].commandStr = command;
      _commands[ndx].functionPtr = (func_ptr_t)function;
      return true;
    }

    /**
     * @brief The primary non-blocking execution entry point.
     * * Checks for new serial data and executes a matching command if a full line 
     * (ending in END_MARKER) is received. Should be called once per loop().
     */
    void update() {
      if (receiveInput()) {
        if (setSelectedFunction()) {
          runSelectedCommand();
        }
      }
    }

    /** @brief Prints all commands currently registered in the command list. */
    void printCommandList() {
      for (uint8_t i = 0; i < MAX_COMMANDS; i++) {
        if (_commands[i].commandStr != nullptr) {
          _device->println(_commands[i].commandStr);
        }
      }
    }

    /**
     * @brief Checks the serial stream for the designated break character to exit a local loop.
     * * This allows a function that is executing its own internal loop to poll the stream 
     * for a termination signal. It uses peek() to check the next available character 
     * without consuming it unless it matches the DEFAULT_BREAK.
     * * @return true if the break character was detected and consumed.
     */
    bool checkForBreak() {
      if (_device->available() > 0) {
        if (_device->peek() == DEFAULT_BREAK) {
          _device->read(); // Consume the break character
          return true; 
        }
      }
      return false;
    }

    /**
     * @brief Returns a pointer to the start of the parameters trailing the command.
     * * Scans for the first space delimiter and increments past any whitespace 
     * to find the actual payload.
     * * @return A pointer to the parameter payload, or nullptr if no parameters exist.
     */
    const char* getParam() {
        const char* buf = getSerialBuffer();
        size_t i = 0;

        // 1. Scan until we hit a space or the end of the null-terminated string
        while (buf[i] != ' ' && buf[i] != '\0') {
            i++;
        }

        // 2. If we found a space, skip over ALL consecutive spaces 
        // to find the start of the actual data.
        while (buf[i] == ' ') {
            i++;
        }

        // 3. If we aren't at the end of the string, this is our parameter start.
        if (buf[i] != '\0') {
            return &buf[i];
        }

        return nullptr; // No valid parameters found
    }

    /**
     * @brief Polls the stream for the next valid character, instantly 
     * flushing any terminators.
     * @return The character read, or 0 if no valid data is available.
     */
    char readChar() {
        while (_device->available() > 0) {
            // If we see the break character, let checkForBreak()
            // handle it on the next loop.
            if (_device->peek() == DEFAULT_BREAK) {
                return 0; 
            }

            // Otherwise, safely consume the character
            char c = _device->read();
            
            // Ignore the end marker so it doesn't interfere with logic loops
            if (c != END_MARKER && c != '\r') {
                return c;
            }
        }
        return 0; // Buffer is empty, or only contained terminators
    }

    /** @brief Prints the current value stored in the _inputBuffer. */
    void printInputBuffer() {
      _device->println(_inputBuffer);
    }

    /** @brief Returns a pointer to the _inputBuffer for use outside of the class. */
    const char* getSerialBuffer() { return _inputBuffer; }

  private:
    typedef void(*func_ptr_t)(void);

    struct CommandRecord {
        const __FlashStringHelper *commandStr;
        func_ptr_t functionPtr;
    };

    // Bitfield masks for state management
    static const uint8_t FLAG_DISCARDING = 0x01;
    static const uint8_t FLAG_INPUT_VALID = 0x02;

    /**
     * @brief Checks the serial stream for available data without blocking. 
     * * If bytes are present, they are appended to _inputBuffer at _bufferIndex.
     * Returns true only when the END_MARKER is detected (completing a command) 
     * or the buffer overflows. Returns false if the command is still incomplete 
     * or no data is available, allowing the main loop to continue.
     */
    bool receiveInput() {
      char rc;

      while (_device->available() > 0) {
        rc = _device->read();

        // Intercept and ignore Windows carriage returns immediately
        if (rc == '\r') continue;

        // Normal processing for all other characters
        if (rc != END_MARKER) {
          if (_flags & FLAG_DISCARDING) continue; 

          // check for buffer overflow
          if (_bufferIndex < BUFFER_SIZE - 1) {
            _inputBuffer[_bufferIndex] = rc;
            _bufferIndex++;
          } else {
            // buffer is full: enter discard state to protect next command
            _inputBuffer[_bufferIndex] = '\0'; // terminate the string
            _flags &= ~FLAG_INPUT_VALID;       // input string too large for buffer
            _flags |= FLAG_DISCARDING;
          }
        } else {
          // end marker reached
          bool wasDiscarding = (_flags & FLAG_DISCARDING);
          _inputBuffer[_bufferIndex] = '\0';
          _bufferIndex = 0; // reset index for the next command
          _flags &= ~FLAG_DISCARDING; // Reset state for next command

          if (wasDiscarding) {
            _flags &= ~FLAG_INPUT_VALID;
            return false; // Silently drop over-sized command
          }

          _flags |= FLAG_INPUT_VALID;
          return true;
        }
      }
      return false; // full command not received yet
    }

    /**
     * @brief Triggers the callback for a successfully matched command string.
     * Typically called internally by update() after receiveInput() and 
     * setSelectedFunction() resolve successfully.
     */
    void runSelectedCommand() {
      if (!(_flags & FLAG_INPUT_VALID) || _functionSelected == nullptr) {
        return;
      }
      (*_functionSelected)();
    }

    /**
     * @brief Sets the function to be selected.
     * * registered functions can't be removed, nullptr is end of list = not found.
     * @return true if a function was found and selected.
     */
    bool setSelectedFunction() {
      _functionSelected = nullptr;

      // Temporarily null-terminate at the first space
      char* spacePos = strchr(_inputBuffer, ' ');
      if (spacePos != nullptr) *spacePos = '\0';

      uint8_t ndx = 0;
      while (ndx < MAX_COMMANDS && (_flags & FLAG_INPUT_VALID)) {
        if (_commands[ndx].commandStr == nullptr) break;

        // Fast-Fail: Check the first character in Flash before executing strcmp_P
        char firstChar = pgm_read_byte(_commands[ndx].commandStr);
        if (firstChar == _inputBuffer[0]) {
          // Exact match check
          if (strcmp_P(_inputBuffer, (PGM_P)_commands[ndx].commandStr) == 0) {
            _functionSelected = _commands[ndx].functionPtr;
            
            // Restore the space so getParam() still works
            if (spacePos != nullptr) *spacePos = ' '; 
            return true;
          }
        }
        ndx++;
      }

      // Restore the space if the command was invalid
      if (spacePos != nullptr) *spacePos = ' '; 
      return false; 
    }

    Stream *_device = nullptr;        ///< Address to input stream.
    uint8_t _bufferIndex = 0;         ///< Current position in _inputBuffer; persists between calls for non-blocking reads.
    uint8_t _flags = 0;               ///< Bitfield storing parser state (FLAG_DISCARDING, FLAG_INPUT_VALID) to minimize SRAM.
    char _inputBuffer[BUFFER_SIZE] = {0}; ///< Input buffer address for stream input. Stored statically (Zero-Heap).
    CommandRecord _commands[MAX_COMMANDS] = {}; ///< Array of structs for better cache locality.
    func_ptr_t _functionSelected = nullptr;     ///< Selected function to be run with runSelectedCommand().
};

#endif /* SERIALCOMMANDCOORDINATOR_h */
