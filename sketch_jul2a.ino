// ARDUINO #2: ТОЛЬКО ПЬЕЗО
float currentFreq = 200.0;

void setup() {
  Serial.begin(9600);
  pinMode(9, OUTPUT);
}
void setPwmFrequency(int pin, float freq);
void loop() { 
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("SET_FREQ=")) {
      // Парсим строку вида "1234.5,5678.9"
      String data = input.substring(9); 
      int commaIndex = data.indexOf(',');
      
      if (commaIndex > 0) {
        float f1 = data.substring(0, commaIndex).toFloat();
        float f2 = data.substring(commaIndex + 1).toFloat();
        
        // Тут твой код установки частот на два выхода
        tone(9, f1,15*1000);
        tone(8, f2,15*1000);
      }
    } 
  }


}
void setPwmFrequency(int pin, float freq)
{  // 2. Генерируем волну НЕПРЕРЫВНО
  float period_us = 1000000.0 / freq;
  unsigned long half_period = (unsigned long)(period_us / 2.0);
  digitalWrite(pin, HIGH);
  delayMicroseconds(half_period); 
  digitalWrite(pin, LOW);
  delayMicroseconds(half_period); 
}


