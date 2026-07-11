#include <SPI.h>
#include <WiFiNINA.h> 

const char* ssid = "MISHAIL";
const char* pass = "misha_superman";
const int port = 80;

WiFiClient client;

// --- ЧАСТОТЫ ---
const float TARGET_FREQ_118 = 118.0;
const unsigned long INTERVAL_US_118 = 1000000UL / (unsigned long)TARGET_FREQ_118;

const float TARGET_FREQ_59 = 59.0;
const unsigned long INTERVAL_US_59 = 1000000UL / (unsigned long)TARGET_FREQ_59;

// --- БУФЕРЫ (Размер + 1 для нуль-терминатора!) ---
const int BUFFER_SIZE_118 = 118;
char workBuffer3_118[BUFFER_SIZE_118 + 1]; // +1 байт под '\0'
char workBuffer5_118[BUFFER_SIZE_118 + 1];
char workBuffer7_118[BUFFER_SIZE_118 + 1];

const int BUFFER_SIZE_59 = 59;
char workBuffer3_59[BUFFER_SIZE_59 + 1];
char workBuffer5_59[BUFFER_SIZE_59 + 1];
char workBuffer7_59[BUFFER_SIZE_59 + 1];

// Буферы для отправки (чтобы не потерять данные при ошибке сети)
char sendBuffer_118[BUFFER_SIZE_118 + 1];
char sendBuffer_59[BUFFER_SIZE_59 + 1];

bool is118Full = false;
bool is59Full = false;
int bufferIndex_118 = 0;
int bufferIndex_59 = 0;

bool dataPending = false; // Флаг: есть данные, готовые к отправке, но не отправленные

unsigned long previousMicros_118 = 0;
unsigned long previousMicros_59 = 0;

// LED
unsigned long ledFlashTimer = 0;
bool ledState = false;
int flashCount = 0;
bool isFlashing = false;
bool flashSuccess = false; 


// --- ДОБАВЬ ЭТИ ПЕРЕМЕННЫЕ В ГЛОБАЛЬНУЮ ОБЛАСТЬ ---
uint8_t lfsrState_118 = 0xAA; 
uint8_t lfsrState_59  = 0x55; 
const uint8_t TAP_MASK = 0b00011101; 

// ... внутри loop(), после записи в буферы (в цикле сбора данных) ...


// Функция обновления LFSR
void lfsrStep(uint8_t* state, uint8_t inputBit) {
  bool feedback = (((*state >> 0) & 1) ^ 
                   ((*state >> 2) & 1) ^ 
                   ((*state >> 3) & 1) ^ 
                   ((*state >> 4) & 1));
  
  *state = (*state >> 1);
  uint8_t newBit = (feedback ^ inputBit) ? 1 : 0;
  if (newBit) {
    *state |= (1 << 7);
  }
}


void setup() {
  pinMode(3, INPUT);
  pinMode(5, INPUT);
  pinMode(7, INPUT);
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);

  Serial.begin(115200);
  while (!Serial);
  
  Serial.print("Connecting to ");
  Serial.println(ssid);
  int status = WiFi.begin(ssid, pass);
  
  while (status != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    status = WiFi.begin(ssid, pass);
  }
  
  Serial.println("\nWiFi connected");
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("System Ready.");
}

void loop() {
  unsigned long currentMicros = micros();

  // --- СБОР 118 Гц ---
  if (currentMicros - previousMicros_118 >= INTERVAL_US_118) {
    previousMicros_118 = currentMicros;
    
    if (!dataPending) { // Пишем только если нет неотправленных данных
      byte v3 = digitalRead(3);
      byte v5 = digitalRead(5);
      byte v7 = digitalRead(7);
      
      // ЗАПИСЫВАЕМ как есть (0 или 1)
      workBuffer3_118[bufferIndex_118] = (v3 ? '1' : '0');
      workBuffer5_118[bufferIndex_118] = (v5 ? '1' : '0');
      workBuffer7_118[bufferIndex_118] = (v7 ? '1' : '0');
      lfsrStep(&lfsrState_118, workBuffer7_118[bufferIndex_118]); // Для 118 Гц
      if (++bufferIndex_118 >= BUFFER_SIZE_118) {
        is118Full = true;
      }
    }
  }

  // --- СБОР 59 Гц ---
  if (currentMicros - previousMicros_59 >= INTERVAL_US_59) {
    previousMicros_59 = currentMicros;

    if (!dataPending) {
      byte v3 = digitalRead(3);
      byte v5 = digitalRead(5);
      byte v7 = digitalRead(7);
      
      workBuffer3_59[bufferIndex_59] = (v3 ? '1' : '0');
      workBuffer5_59[bufferIndex_59] = (v5 ? '1' : '0');
      workBuffer7_59[bufferIndex_59] = (v7 ? '1' : '0');
      // Не забудь добавить этот вызов после записи бита в буфер!

      lfsrStep(&lfsrState_59, workBuffer7_59[bufferIndex_59]);  // Для 59 Гц
      if (++bufferIndex_59 >= BUFFER_SIZE_59) {
        is59Full = true;
      }
    }
  }
  
  // --- ЛОГИКА ОТПРАВКИ ---
  // Если буферы полны ИЛИ у нас есть данные, которые не удалось отправить ранее
  if ((is118Full && is59Full) || dataPending) {
    if (!dataPending) {
      // Данные только что собраны -> формируем итоговую строку с голосованием
      applyMajorityVoting();
      dataPending = true; // Помечаем, что данные готовы к отправке
      is118Full = false; is59Full = false; // Сбрасываем флаги сбора, чтобы начать новые
      bufferIndex_118 = 0; bufferIndex_59 = 0;
    }
    
    // Пытаемся отправить. Если успешно -> снимаем флаг pending.
    if (sendToServer()) {
      startLedFlash(true);
      dataPending = false; // Данные ушли, можно собирать новые
    } else {
      startLedFlash(false);
      // dataPending остается TRUE! Мы НЕ сбрасываем буферы. 
      // В следующий цикл loop() мы снова попробуем отправить ТЕ ЖЕ данные.
      Serial.println("[WARN] Data not sent. Will retry next cycle.");
    }
  }

  handleLedIndication(millis());
}

// МАЖОРИТАРНОЕ ГОЛОСОВАНИЕ (2 из 3)
// Это решает проблему рассинхрона сенсоров
void applyMajorityVoting() {
  // Обрабатываем 118 Гц
  for (int i = 0; i < BUFFER_SIZE_118; i++) {
    int sum = (workBuffer3_118[i] - '0') + (workBuffer5_118[i] - '0') + (workBuffer7_118[i] - '0');
    sendBuffer_118[i] = (sum >= 2) ? '1' : '0';
  }
  sendBuffer_118[BUFFER_SIZE_118] = '\0'; // ВАЖНО: ставим конец строки!

  // Обрабатываем 59 Гц
  for (int i = 0; i < BUFFER_SIZE_59; i++) {
    int sum = (workBuffer3_59[i] - '0') + (workBuffer5_59[i] - '0') + (workBuffer7_59[i] - '0');
    sendBuffer_59[i] = (sum >= 2) ? '1' : '0';
  }
  sendBuffer_59[BUFFER_SIZE_59] = '\0'; // ВАЖНО: ставим конец строки!
  
  Serial.println("[OK] Majority voting applied. Ready to send.");
}
// --- ИСПРАВЛЕННАЯ ФУНКЦИЯ ОТПРАВКИ ---
bool sendToServer() {
  if (!client.connect("xn--80aah6bn6b.xn--90ais", port)) {
    Serial.println("[ERROR] Connection failed");
    return false;
  }

  // ВОТ ЗДЕСЬ МЫ ВОЗВРАЩАЕМ СУММЫ!
  String body = "y_118=" + String(sendBuffer_118) + 
                "&x_59=" + String(sendBuffer_59) +
                "&s_118=" + String(lfsrState_118, HEX) + // <-- Добавлено
                "&s_59=" + String(lfsrState_59, HEX);     // <-- Добавлено
  
  int contentLength = body.length();
 
  String request = "POST /Data/Receive HTTP/1.1\r\n";
  request += "Host: xn--80aah6bn6b.xn--90ais\r\n";
  request += "Content-Type: application/x-www-form-urlencoded\r\n";
  request += "Content-Length: " + String(contentLength) + "\r\n";
  request += "Connection: close\r\n\r\n"; 

  client.print(request);
  client.print(body);

  Serial.println("Packet sent. Waiting for response...");

  unsigned long timeout = millis();
  bool responseReceived = false;
  String response = "";

  while (millis() - timeout < 5000) {
    while(client.available()){
      char c = client.read();
      response += c;
      responseReceived = true;
    }
    if (responseReceived) break;
    delay(10);
  }
  
  client.stop();

  if (!responseReceived) {
    Serial.println(">>> Timeout!");
    return false;
  }

  if (response.indexOf("200 OK") != -1 || response.indexOf("Valid") != -1) {
    Serial.println(">>> Server confirmed!");
    
    // ВАЖНО: Если сервер принял, нужно СБРОСИТЬ LFSR!
    // Иначе следующая сумма будет считаться от старой, и сервер скажет "Checksum mismatch"
    lfsrState_118 = 0xAA; 
    lfsrState_59 = 0x55;
    
    return true;
  } else {
    Serial.println(">>> Server rejected: " + response);
    return false;
  }
}

void resetBuffers() {
  is118Full = false; is59Full = false;
  bufferIndex_118 = 0; bufferIndex_59 = 0;
}

void startLedFlash(bool success) {
  flashSuccess = success; 
  isFlashing = true; 
  ledState = false; 
  flashCount = 0; 
  ledFlashTimer = millis();
}

void handleLedIndication(unsigned long currentMillis) {
  if (!isFlashing) return;
  unsigned long interval = flashSuccess ? 200 : 100;
  
  if (currentMillis - ledFlashTimer >= interval) {
    ledFlashTimer = currentMillis;
    if (flashSuccess) {
      if (!ledState) { digitalWrite(13, HIGH); ledState = true; }
      else { digitalWrite(13, LOW); isFlashing = false; }
    } else {
      digitalWrite(13, !digitalRead(13));
      flashCount++;
      if (flashCount >= 6) { digitalWrite(13, LOW); isFlashing = false; }
    }
  }
}
