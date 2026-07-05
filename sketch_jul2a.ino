 
// НАСТРОЙКИ ТАЙМЕРА
const unsigned long INTERVAL_US = 8475UL; // ~117 Гц (подбирай под свой датчик)
unsigned long previousMicros = 0;
// Счетчик для отладки частоты
volatile uint32_t sampleCount = 0;
unsigned long lastCheckTime = 0;
const int piezoPin = 5; // выберите любой цифровой пин, совместимый с tone()
const int sensorPin = A2;
const int PIEZO_PIN =5;

const int BUFFER_SIZE = 118;        // Размер буфера (чуть больше 118 для запаса)
uint8_t dataBuffer[BUFFER_SIZE];    // Буфер для хранения данных (1 байт на значение)
int bufferIndex = 0;                // Индекс текущей записи
bool dataReady = false;             // Флаг: данные собраны?

float currentFreq = 1;
// --- НАСТРОЙКИ ПОД НОВЫЙ СЕНСОР ---
const float alpha = 0.03;       // Скорость адаптации нуля
const float threshold_on = 0.5; // ПОРОГ: СНИЖЕН! Твои данные теперь ~30-40, так что 15 - это хороший старт.
                                 // Если будет много ложных срабатываний - поставь 20. Если не срабатывает - поставь 10.
const unsigned long lockoutTime = 10; // Блокировка: 500 мс тишины после импульса
unsigned long lastChangeTime = 0;
float adaptiveZero = 0.0;
unsigned long lastTriggerTime = 0; 

void sendData();

void setup() {
  Serial.begin(9600);
  while (!Serial); 
  
  // Активируем систему
} 
float newFreq;
const unsigned long MIN_CHANGE_INTERVAL = 50;
void activatePiezo(float frequency, float durationSeconds);
void loop() {
  unsigned long currentMicros = micros();

  // 1. ЧЕТКИЙ ТАЙМЕР (Гарантирует частоту дискретизации)
  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros = currentMicros;
    

   // 1. Читаем датчик
  int rawValue = analogRead(sensorPin);
  float currentVal = (float)rawValue;

  // 2. Считаем "Дзен" (адаптивный ноль)
  adaptiveZero = adaptiveZero * (1.0 - alpha) + currentVal * alpha;
  
  // Считаем отклонение
  float deviation = currentVal - adaptiveZero;
  float absDev = abs(deviation);

  unsigned long now = millis();
  bool shouldOutputOne = false;

  // 3. ЛОГИКА: Одиночный импульс
  // Если прошло время блокировки И отклонение больше порога
  if ((now - lastTriggerTime >= lockoutTime) && (absDev > threshold_on)) {
    shouldOutputOne = true;
    lastTriggerTime = now; // Запоминаем время, чтобы следующие 500мс выдавать 0
  }

  //bci.feedSensorData(shouldOutputOne);

  // 3. Обрабатываем команды из компьютера (если есть)
    dataBuffer[bufferIndex] = (uint8_t)shouldOutputOne+0x30; 
    
    bufferIndex++;

    // 2. Если буфер заполнен, помечаем как готовый и сбрасываем индекс
    if (bufferIndex >= BUFFER_SIZE) {
      bufferIndex = 0;
      dataReady = true;
    }
    sampleCount++;
  }

  // 6. Отладка частоты дискретизации (раз в секунду)
  unsigned long now = millis();

  if (now - lastCheckTime >= 1000) {
    lastCheckTime = now;
    // Serial.print(F("SAMPLES_PER_SEC: "));
    //Serial.println(sampleCount);
    sampleCount = 0;

 
  }







    // 3. Обработка команд от ПК
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim(); // Убираем пробелы и переносы строк

    if (command == "GET_DATA") {
      sendData();
    } 
   else if (command.startsWith("SET_FREQ=")) {
      // Парсим частоту
        newFreq = command.substring(9).toFloat();
      
      // Защита: не меняем частоту слишком часто, даем системе время отреагировать
      if (millis() - lastChangeTime > MIN_CHANGE_INTERVAL) {
        currentFreq = newFreq;
        lastChangeTime = millis();
      }

    } 
    else {
      Serial.println("Unknown command. Use: GET_DATA");
    }
 
    }
 
 

}
 
// Функция отправки данных
void sendData() {
  if (!dataReady) {
    Serial.println("NO_DATA_YET"); // Данные еще не накопились
    return;
  }
  dataReady = false; // Сбрасываем флаг, так как отправляем текущие данные
  // Вариант А: Отправка бинарных данных (самый быстрый и компактный)
  // ПК должен уметь читать бинарные данные, а не текст.
  Serial.write(dataBuffer, BUFFER_SIZE);
Serial.println();
}
