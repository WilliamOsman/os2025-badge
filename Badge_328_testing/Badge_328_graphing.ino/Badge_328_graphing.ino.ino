#define PHOTODIODE A0
#define LED_PIN 7
#define CENTER_PIN 6

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(CENTER_PIN, OUTPUT);
  pinMode(PHOTODIODE, INPUT_PULLUP);
}

void loop() {
  static float data_smooth = 0;

  // --------------- Phototransistor AutoExposure and ADC --------------------

  //charge up the photodiode's capacitance
  pinMode(PHOTODIODE, INPUT_PULLUP);
  delayMicroseconds(100);
  pinMode(PHOTODIODE, INPUT);
  //delayMicroseconds(500);


  //read voltage at fixed time delay after charge up
  //uint16_t newdata = analogRead(PHOTODIODE);
  uint32_t newdata = 0;
  uint32_t time = micros();
  for (uint8_t i = 0; i < 64; i++) {
    newdata += analogRead(PHOTODIODE);
  }
  time = micros() - time;

  newdata /= 64;

  // smoothing average
  data_smooth = data_smooth * 0.6 + newdata * 0.4;

  static float maximum = newdata;
  static float minimum = newdata;

  //calculate data envelope
  if (newdata > maximum) maximum = newdata;
  else if (newdata < minimum) minimum = newdata;
  //slow decay of max/min to hug data
  else {
    maximum = maximum - maximum * 0.005;
    minimum = minimum + maximum * 0.005;
  }
  float amplitude = (maximum - minimum) / 2 + minimum;

  bool state = 0;
  if (data_smooth > amplitude) state = 1;

    //long average
    //photodata_average = photodata_average * 0.6 + newdata * 0.4;

    Serial.print(time);
  Serial.print(",");
  Serial.print(newdata);
  Serial.print(",");
  Serial.print(data_smooth);
  Serial.print(",");
  Serial.print(maximum);
  Serial.print(",");
  Serial.print(minimum);
  Serial.print(",");
  Serial.print(amplitude);
  Serial.print(",");
  Serial.print(state * 50);
  Serial.println();

  //delay(6);
}