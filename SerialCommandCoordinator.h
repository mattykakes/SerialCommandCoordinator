/**
 * @file SerialCommandCoordinator.h
 * @author Matthew Miller
 * @brief A memory-efficient, non-blocking serial command dispatcher for Arduino.
 * @version 0.1.0
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
    
    virtual ~SerialCommandCoordinator() {}

    /**
     * @brief Given a null terminated string and function address, attempts to register 
     * a command with its intended routine. 
     * * @param command A string literal wrapped in the F() macro.
     * @param function Pointer to a void function with no parameters.
     * @return Fails and returns false if the command is already in the list, 
     * the list is full, or nullptr is an argument. Returns true on success.
     */
bool registerCommand(const __FlashStringHelper *command, void (*function)(void)) {      if (command == nullptr || function == nullptr) {
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
     * (ending in END_MARKER) is received. Should be called once per loop().
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
     * @brief Polls the stream for a single character, ignoring the END_MARKER.
     * @return The character read, or 0 if no data is available.
     */
    char readChar() {
        if (_device->available() > 0) {
            char c = _device->read();
            // Ignore the end marker so it doesn't interfere with logic loops
            if (c != END_MARKER && c != '\r') {
                return c;
            }
        }
        return 0;
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
      _functionSelected = nullptr;

      // Temporarily null-terminate at the first space
      uint8_t* spacePos = strchr(_inputBuffer, ' ');
      if (spacePos != nullptr) *spacePos = '\0';

      int ndx = 0;
      while (ndx < MAX_COMMANDS && _inputValid) {
        if (_commandList[ndx] == nullptr) break;

        // Exact match check
        if (strcmp_P(_inputBuffer, (PGM_P)_commandList[ndx]) == 0) {
          _functionSelected = _functionList[ndx];
          
          // Restore the space so getParam() still works
          if (spacePos != nullptr) *spacePos = ' '; 
          return true;
        }
        ndx++;
      }

      // Restore the space if the command was invalid
      if (spacePos != nullptr) *spacePos = ' '; 
      return false; 
    }

    Stream *_device = nullptr;        ///< Address to input stream.
    uint8_t _bufferIndex = 0;         ///< Current position in _inputBuffer; persists between calls for non-blocking reads.
    bool _discarding = false;         ///< State used to ignore characters after an overflow until END_MARKER.

    bool _inputValid = false;         ///< State of input buffer fitting entirely within the _inputBuffer.
    char _inputBuffer[BUFFER_SIZE] = {0}; ///< Input buffer address for stream input. Stored statically (Zero-Heap).

    const __FlashStringHelper *_commandList[MAX_COMMANDS] = {nullptr}; ///< List of addresses for registered command strings stored in flash.
    func_ptr_t _functionList[MAX_COMMANDS] = {nullptr};                ///< List of addresses for registered functions.
    func_ptr_t _functionSelected = nullptr;                            ///< Selected function to be run with runSelectedCommand().
};

#endif /* SERIALCOMMANDCOORDINATOR_h */