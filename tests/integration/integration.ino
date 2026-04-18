#include <../../SerialCommandCoordinator.h>

// 10 commands, 64-byte buffer, '!' break, '\n' end marker
SerialCommandCoordinator<10, 64, '!', '\n'> scc(Serial);

void setup() {
  Serial.begin(115200);

  // TEST 1 & 2: Command WITHOUT parameters (Handles \n and \r\n)
  scc.addCommand("ping", [](auto& s) {
    Serial.println(F("PONG"));
  });

  // TEST 3 & 4: Command WITH parameters (Handles valid and missing values)
  scc.addCommand("set-limit", [](auto& s) {
    const char* val = s.getParam();
    if (val) {
      Serial.print(F("LIMIT_SET:"));
      Serial.println(val);
    } else {
      Serial.println(F("ERROR:MISSING_VAL"));
    }
  });

  // TEST 5: Status check (Verifies recovery after buffer overflow)
  scc.addCommand("status", [](auto& s) {
    Serial.println(F("STATUS:OK"));
  });

  // TEST 6: Interactive Command (Sub-Mode)
  scc.addCommand("jog", [](auto& s) {
    runManualJog();
  });

  Serial.println(F("SYSTEM_READY"));
}

void runManualJog() {
  Serial.println(F("MODE:JOG"));
  while (true) {
    // TEST 7: Exit back to main loop
    if (scc.checkForBreak()) { 
      Serial.println(F("MODE:MAIN"));
      break;
    }

    char c = scc.readChar(); 
    if (c == '+') Serial.println(F("UP"));
    else if (c == '-') Serial.println(F("DOWN"));
  }
}

void loop() {
  scc.update();
}