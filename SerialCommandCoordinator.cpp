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
#include <Arduino.h>
#include "SerialCommandCoordinator.h"

SerialCommandCoordinator::SerialCommandCoordinator(Stream &device) : _device(&device) {
  init();
}

SerialCommandCoordinator::SerialCommandCoordinator(Stream *device) : _device(device) {
  init();
}

SerialCommandCoordinator::~SerialCommandCoordinator() {
  if (_inputBuffer) {
    free(_inputBuffer);
  }
  
  // currently manages all command string memory
  if (_commandList) {
    for (int i = 0; i < _commandListSize; i++) {
      if (_commandList[i]) {
        free(_commandList[i]);
      }
    }
    free(_commandList);
  }

  if (_functionList) {
    free(_functionList);
  }
}

bool SerialCommandCoordinator::receiveInput() {
  char rc;

  if (_device->available() > 0){
    rc = _device->read();

    if (rc != _endMarker) {
      // check for buffer overflow
      if (_bufferIndex < _inputBufferSize - 1) {
        _inputBuffer[_bufferIndex] = rc;
        _bufferIndex++;
      } else {
        // buffer is full
        _inputBuffer[_bufferIndex] = '\0'; // terminate the string
        _inputValid = false; // input string too large for buffer
        _bufferIndex = 0;
        return true; // 
      }
    } else {
      // end marker reached
      _inputBuffer[_bufferIndex] = '\0';
      _bufferIndex = 0; // reset index for the next command
      _inputValid = true;
      return true;
    }
  }
  return false; // full command not recieved yet
}

bool SerialCommandCoordinator::receiveCommandInput() {
  if (receiveInput()) {
    return setSelectedFunction();
  }
  return false;
}

void SerialCommandCoordinator::printInputBuffer() {
  _device->println(_inputBuffer);
}

bool SerialCommandCoordinator::registerCommand(const char *command, const void (*function)(void)) {
  if (command == nullptr || function == nullptr) {
    return false;
  }
  
  // find next empty spot in list
  int ndx = 0;
  while (ndx < _commandListSize) {
    // find next empty spot in list
    if (_commandList[ndx] == nullptr) {
      break;

    // command already in list
    } else if (strcmp(_commandList[ndx], command) == 0) {
      return false;
    }
    ndx++;
  }

  // Command buffer full, cannot register command;
  if (ndx >= _commandListSize) {
    return false; 
  }

  // Store command and function address
  int size = strlen(command) + 1;
  _commandList[ndx] = (char*) malloc(size * sizeof(char));
  if (_commandList[ndx] == nullptr) {
    return false;
  }
  strcpy(_commandList[ndx], command);
  _functionList[ndx] = function;
  return true;

}

void SerialCommandCoordinator::update() {
  if (receiveCommandInput()) {
    runSelectedCommand();
  }
}

void SerialCommandCoordinator::runSelectedCommand() {
  if (!_inputValid || _functionSelected == nullptr) {
    return;
  }
  (*_functionSelected)();
}

void SerialCommandCoordinator::printCommandList() {
  for (int i = 0; i < _commandListSize; i++) {
    if (_commandList[i] != nullptr) {
      _device->println(_commandList[i]);
    }
  }
}

void SerialCommandCoordinator::testStream() {
  _device->println("Hello World!"); 
}

void SerialCommandCoordinator::init() {
  _inputBuffer = (char*) calloc(_inputBufferSize, sizeof(char));
  _commandList = (char**) calloc(_commandListSize, sizeof(char*));
  _functionList = (func_ptr_t*) calloc(_commandListSize, sizeof(func_ptr_t*));

  if (_inputBuffer == nullptr || _commandList == nullptr || _functionList == nullptr) {
    abort();
  }
}

bool SerialCommandCoordinator::setSelectedFunction() {
  int ndx = 0;
  _functionSelected = nullptr;

  while (ndx < _commandListSize && _inputValid) {

    // registered functions can't be removed, nullptr is end of list = not found
    if (_commandList[ndx] == nullptr) {
      return false;
    }

    // found function
    if (strcmp(_inputBuffer, _commandList[ndx]) == 0) {
      _functionSelected = _functionList[ndx];
      return true;
    }
    ndx++;    
  }

  // not found in list
  return false;
}
