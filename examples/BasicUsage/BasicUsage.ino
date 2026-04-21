#include <SerialCommandCoordinator.h>

// 10 commands, 64-byte buffer, '!' break, '\n' end marker 
SerialCommandCoordinator<10, 64, '!', '\n'> scc(Serial);

// Wrappers to convert member calls to void pointers for registerCommand
void handlePing() {
  Serial.println(F("PONG"));
}

void handleSetLimit() {
  const char* val = scc.getParam(); // Access global scc
  if (val) {
    Serial.print(F("LIMIT_SET:"));
    Serial.println(val);
  } else {
    Serial.println(F("ERROR:MISSING_VAL"));
  }
}

void handleStatus() {
  Serial.println(F("STATUS:OK"));
}

void runManualJog() {
  Serial.println(F("MODE:JOG"));
  while (true) {
    // TEST 7: Exit back to main loop via Break Character
    if (scc.checkForBreak()) { 
      Serial.println(F("MODE:MAIN"));
      break;
    }

    char c = scc.readChar(); 
    if (c == '+') Serial.println(F("UP"));
    else if (c == '-') Serial.println(F("DOWN"));
  }
}

void handleJog() {
  runManualJog();
}

void handleHelp() {
  scc.printCommandList();
}

void setup() {
  Serial.begin(115200);

  // TEST 1 & 2: Wrap strings in F() to match __FlashStringHelper*
  scc.registerCommand(F("ping"), handlePing);

  // TEST 3 & 4: Command WITH parameters
  scc.registerCommand(F("set-limit"), handleSetLimit);

  // TEST 5: Status check
  scc.registerCommand(F("status"), handleStatus);

  // TEST 6: Interactive Command
  scc.registerCommand(F("jog"), handleJog);

  // TEST 8: Print Command List
  scc.registerCommand(F("help"), handleHelp);

  Serial.println(F("SYSTEM_READY"));
}

void loop() {
  scc.update();
}