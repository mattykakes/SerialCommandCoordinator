#include <SerialCommandCoordinator.h>

// 10 commands, 64-byte buffer, '!' break, '\n' end marker
SerialCommandCoordinator<10, 64, '!', '\n'> scc(Serial);

void setup() {
  Serial.begin(115200);

  // TEST 1: Command WITHOUT parameters (Simple Trigger)
  scc.addCommand("ping", [](auto& s) {
    Serial.println(F("PONG"));
  });

  // TEST 2: Command WITH parameters (Data Entry)
  scc.addCommand("set-limit", [](auto& s) {
    const char* val = s.getParam();
    if (val) {
      Serial.print(F("LIMIT_SET:"));
      Serial.println(val);
    } else {
      Serial.println(F("ERROR:MISSING_VAL"));
    }
  });

  // TEST 3: Interactive Command (Sub-Mode)
  scc.addCommand("jog", [](auto& s) {
    runManualJog();
  });

  Serial.println(F("SYSTEM_READY"));
}

void runManualJog() {
  Serial.println(F("MODE:JOG"));
  while (true) {
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