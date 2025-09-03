#define PHOTODIODE A0
#define LED_PIN 7
#define CENTER_PIN 6

#define DATALENGTH 8
#define SHIFT(cur, shift) (cur + shift) & (DATALENGTH - 1)

//uint16_t data[DATALENGTH];
uint8_t read_index = 0;
uint8_t write_index = 0;

#define FRAME_WIDTH 6
#define MAX_FRAMES 12
uint8_t data_buf[FRAME_WIDTH * MAX_FRAMES] = { 0 };  // frame buffer
uint8_t data_frame_count = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(CENTER_PIN, OUTPUT);
  pinMode(PHOTODIODE, INPUT_PULLUP);
}

void loop() {
  static uint16_t photodata[DATALENGTH];
  static bool data[DATALENGTH];
  static uint8_t rx = 0b0;
  static bool noData = true;

  uint32_t newdata = 0;
  static float data_smooth = 0;
  static float data_max = 0;
  static float data_min = 1024;

  float amplitude = 0;
  float deviation = 0;
  bool state = 0;
  static bool state_prev = 0;

  // --------------- Phototransistor AutoExposure and ADC --------------------
  do {
    //charge up the photodiode's capacitance
    pinMode(PHOTODIODE, INPUT_PULLUP);
    delayMicroseconds(100);
    pinMode(PHOTODIODE, INPUT);
    //delayMicroseconds(500);

    //integrate as voltage drops due to photodiode
    uint32_t startTime = micros();
    for (uint8_t i = 0; i < 64; i++) {
      newdata += analogRead(PHOTODIODE);
    }
    newdata /= 64;  //average

    // exponential average to smooth data
    data_smooth = data_smooth * 0.5 + newdata * 0.5;

    // track maximum/minimum values
    if (newdata > data_max) data_max = newdata;
    else if (newdata < data_min) data_min = newdata;
    else {
      data_max = data_max - data_max * 0.005;
      data_min = data_min + data_max * 0.005;
    }
    float data_nuetral = (data_max - data_min) / 2 + data_min;
    amplitude = (data_max - data_min);

    if (data_smooth < data_nuetral) state = 1;  //invert the reading
    else state = 0;

    /*
    Serial.print(newdata);
    Serial.print(",");
    Serial.print(data_smooth);
    Serial.print(",");
    Serial.print(data_max);
    Serial.print(",");
    Serial.print(data_min);
    Serial.print(",");
    Serial.print(data_nuetral);
    Serial.print(",");
    Serial.print(state * 100);
    Serial.println();
    */

  } while (amplitude < 50);

  //------------- Filter Phototransistor readings -------------------------
  /*
  //replace ambiguous readings with the previous measurement
  /
  if (newdata < (data_max - deviation) && newdata > (rolling_min + deviation)) {
    photodata[write_index] = photodata[SHIFT(write_index, -1)];
  } else photodata[write_index] = newdata;

  //data_smooth = data_smooth * 0.2 + newdata * 0.8;
  data_smooth = data_smooth * 0.8 + newdata * 0.2;

  if (data_smooth > data_average) data[write_index] = 1;
  else data[write_index] = 0;

  //correct for single flipped bits
  if (data[SHIFT(write_index, -2)] == data[write_index]) {
    data[SHIFT(write_index, -1)] = data[write_index];
  }
*/
  //--------------- Convert to Bitstream ------------
  Serial.print(state);

  //calculate bit width
  static float period = 8;
  static uint8_t halfperiod = period / 2;
  static uint8_t bits = 1;
  static uint8_t width = 0;
  static uint8_t next_bit = period;
  static bool newbitFlag = false;

  //detect bit on edge
  if (state != state_prev) {
    state_prev = state;
    Serial.print(" E");  //E for edge
    //write the first bit
    rx = (rx << 1) | state;  //load next bit
    newbitFlag = true;
    next_bit = period + halfperiod;
    bits = 1;
    width = 1;
  }

  //detect bit on center
  else if (width == next_bit) {
    digitalWrite(CENTER_PIN, HIGH);
    Serial.print(" W");      //W for center trigger
    rx = (rx << 1) | state;  //invert and load next bit
    newbitFlag = true;
    digitalWrite(CENTER_PIN, LOW);
    next_bit += period;
    bits++;
  }

  width++;

  //----------- Process Bitstream ----------------------
  if (newbitFlag) {
    static bool readData = false;
    newbitFlag = false;
    if (!readData && (rx & (0b1111)) == 0b1110) {
      Serial.print("  data start");
      readData = true;
      rx = 0b0;  //clear RX
      data_buf[0] = 0b0;
    }

    else if (readData) {
      static uint8_t current_bit = 0;
      static uint8_t current_byte = 0;

      Serial.print("B:");
      Serial.print(current_byte);
      Serial.print(":");
      Serial.print(current_bit);

      //Serial.println(rx);
      data_buf[current_byte] |= (state << current_bit);
      Serial.print(":");
      Serial.print(data_buf[current_byte], BIN);
      //Serial.println(rx & 0b1);

      current_bit++;

      if (current_bit > 5) {
        //if 5th bit is 0, data is over, or read is corrupted
        if (!(rx & 0b1)) {
          //data read is finished, or data is corrupted
          readData = false;
          Serial.print(":TERMINATED:");
          if ((current_byte + 1) % FRAME_WIDTH != 0) {
            Serial.print("  PARTIAL FRAME ");
            //return 0;	// not a full frame detected
          } else if (current_bit != 6) {
            Serial.print("PARTIAL COLUMN");
            //return 0;	// didn't end on a full vertical line (minus the stop bit)
          } else if ((current_byte + 1) / FRAME_WIDTH > MAX_FRAMES) {
            Serial.println("TOO MANY FRAMES");
            //return 0;	// too many frames
          } else Serial.println("Sucess");
          //print out transmitted data
          for (uint8_t i = 0; i <= current_byte; i++) {
            Serial.println(data_buf[i], BIN);
          }
          
          //reset for new data transmission
          current_byte = 0;
          current_bit = 0;
          
        } 
        //finished reading column, move onto next byte
        else {
          Serial.print(":");
          Serial.print(data_buf[current_byte], BIN);
          current_byte++;
          current_bit = 0;
          data_buf[current_byte] = 0b0;
        }
      }
    }
  }
  Serial.println();
  digitalWrite(LED_PIN, state);

 // write_index = SHIFT(write_index, 1);

  delay(5);
}