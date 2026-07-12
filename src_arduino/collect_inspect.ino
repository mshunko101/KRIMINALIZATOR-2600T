const float TARGET_FREQ_118 = 118.0;
const unsigned long INTERVAL_US_118 = 1000000UL / (unsigned long)TARGET_FREQ_118;

const float TARGET_FREQ_59 = 59.0;
const unsigned long INTERVAL_US_59 = 1000000UL / (unsigned long)TARGET_FREQ_59;

const int SENSOR_PIN3 = 3;
const int SENSOR_PIN5 = 5;
const int SENSOR_PIN7 = 7;
const int LED_PIN = 13;

// --- Рабочие буферы (для сбора данных прямо сейчас) ---
const int BUFFER_SIZE_118 = 118;
char workBuffer3_118[BUFFER_SIZE_118];
char workBuffer5_118[BUFFER_SIZE_118];
char workBuffer7_118[BUFFER_SIZE_118];

const int BUFFER_SIZE_59 = 59;
char workBuffer3_59[BUFFER_SIZE_59];
char workBuffer5_59[BUFFER_SIZE_59];
char workBuffer7_59[BUFFER_SIZE_59];

// --- СОХРАНЕННЫЕ БУФЕРЫ (ОДНА КОПИЯ НА КАЖДУЮ ЧАСТОТУ) ---
char savedBuffer_118[BUFFER_SIZE_118];
char savedBuffer_59[BUFFER_SIZE_59];

// Флаг готовности данных (становится true ТОЛЬКО если оба буфера валидны и равны друг другу)
bool dataReady = false; 

unsigned long previousMicros_118 = 0;
unsigned long previousMicros_59 = 0;

int bufferIndex_118 = 0;
int bufferIndex_59 = 0;

void setup() {
  pinMode(SENSOR_PIN3, INPUT);
  pinMode(SENSOR_PIN5, INPUT);
  pinMode(SENSOR_PIN7, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial);
  Serial.println("System Ready. Waiting for synchronized data...");
  Serial.println("Commands: GET_118, GET_59");
}
  bool is118Full = false;
  bool is59Full = false;
void loop() {
  unsigned long currentMicros = micros();


  // === СБОР 118 Гц ===
  if (currentMicros - previousMicros_118 >= INTERVAL_US_118) {
    previousMicros_118 = currentMicros;
    workBuffer3_118[bufferIndex_118] = digitalRead(SENSOR_PIN3) ? '1' : '0';
    workBuffer5_118[bufferIndex_118] = digitalRead(SENSOR_PIN5) ? '1' : '0';
    workBuffer7_118[bufferIndex_118] = digitalRead(SENSOR_PIN7) ? '1' : '0';
    bufferIndex_118++;

    if (bufferIndex_118 >= BUFFER_SIZE_118) {
      is118Full = true;
      bufferIndex_118 = 0; // Сброс для следующего цикла
    }
  }

  // === СБОР 59 Гц ===
  if (currentMicros - previousMicros_59 >= INTERVAL_US_59) {
    previousMicros_59 = currentMicros;
    workBuffer3_59[bufferIndex_59] = digitalRead(SENSOR_PIN3) ? '1' : '0';
    workBuffer5_59[bufferIndex_59] = digitalRead(SENSOR_PIN5) ? '1' : '0';
    workBuffer7_59[bufferIndex_59] = digitalRead(SENSOR_PIN7) ? '1' : '0';
    bufferIndex_59++;

    if (bufferIndex_59 >= BUFFER_SIZE_59) {
      is59Full = true;
      bufferIndex_59 = 0; // Сброс для следующего цикла
    }
  }

  // === ГЛАВНАЯ ЛОГИКА ПРОВЕРКИ (Только если ОБА буфера заполнились в этом проходе) ===
  if (is118Full && is59Full) {
    processBothBuffers();
  }

  handleSerialCommands();
}

// Функция проверки всех условий
void processBothBuffers() {
  // 1. Проверка внутри 118 Гц (все 3 датчика равны)
  bool match118 = true;
  for (int i = 0; i < BUFFER_SIZE_118; i++) {
    if (workBuffer3_118[i] != workBuffer5_118[i] || workBuffer3_118[i] != workBuffer7_118[i]) {
      match118 = false;
      break;
    }
  }

  // 2. Проверка внутри 59 Гц (все 3 датчика равны)
  bool match59 = true;
  for (int i = 0; i < BUFFER_SIZE_59; i++) {
    if (workBuffer3_59[i] != workBuffer5_59[i] || workBuffer3_59[i] != workBuffer7_59[i]) {
      match59 = false;
      break;
    }
  }
 

  // === ИТОГОВОЕ РЕШЕНИЕ ===
  if (match118 && match59) {
    // ВСЁ СОВПАЛО! Копируем в хранилище.
    memcpy(savedBuffer_118, workBuffer3_118, BUFFER_SIZE_118);
    memcpy(savedBuffer_59, workBuffer3_59, BUFFER_SIZE_59);
    
    dataReady = true; // Флаг успеха
    
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    
    Serial.println("[SUCCESS] Both buffers validated and saved!");
  } else {
    // ЧТО-ТО НЕ СОВПАЛО. Данные отбрасываем.
    dataReady = false; // Флаг неудачи (на случай если был true ранее)
    
    // Мигание ошибкой
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH); delay(100); digitalWrite(LED_PIN, LOW); delay(100);
    }
    
    Serial.print("[FAIL] Data discarded. Reasons: ");
    if (!match118) Serial.print("118Hz mismatch | ");
    if (!match59) Serial.print("59Hz mismatch | ");
    Serial.println();
  }

   is118Full = false;
   is59Full = false;
}

void handleSerialCommands() {
  static String inputString = "";
  
  if (Serial.available()) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n' || inChar == '\r') {
      inputString.trim();
      
      if (!dataReady) {
        Serial.println("NO_DATA: No valid synchronized data available yet.");
      } else {
        if (inputString == "GET_118") {
          Serial.print("DATA_118: ");
          Serial.write(savedBuffer_118, BUFFER_SIZE_118);
          Serial.println();
        } 
        else if (inputString == "GET_59") {
          Serial.print("DATA_59: ");
          Serial.write(savedBuffer_59, BUFFER_SIZE_59);
          Serial.println();
        }
        else {
          Serial.println("Unknown command. Use: GET_118 or GET_59");
        }
      }
      inputString = "";
    } 
    else {
      inputString += inChar;
    }
  }
}
