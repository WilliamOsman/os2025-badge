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

  static uint16_t newdata = 0;
  static float photodata_average = 0;
  static float photodata_smooth = 0;
  static float photodata_max = 0;
  static float photodata_min = 1024;
  static float rolling_max = 0;
  static float rolling_min = 0;

  float amplitude = 0;
  float deviation = 0;

  // --------------- Phototransistor AutoExposure and ADC --------------------
  do {
    //charge up the photodiode's capacitance
    pinMode(PHOTODIODE, INPUT_PULLUP);
    delayMicroseconds(100);
    pinMode(PHOTODIODE, INPUT);
    delayMicroseconds(500);

    //read voltage at fixed time delay after charge up
    newdata = analogRead(PHOTODIODE);

    // exponential average
    photodata_average = photodata_average * 0.98 + newdata * 0.02;

    // track maximum/minimum values
    if (newdata > photodata_max) photodata_max = newdata;
    else if (newdata < photodata_min) photodata_min = newdata;
    amplitude = (photodata_max - photodata_min);
    Serial.println(amplitude);
    deviation = amplitude * 0.2;
    if (newdata > (photodata_min + amplitude - deviation)) rolling_max = rolling_max * 0.9 + newdata * 0.1;
    else if (newdata < (photodata_min + deviation)) rolling_min = rolling_min * 0.95 + newdata * 0.05;

    photodata_min += amplitude * 0.01;  //slow decay to hug bottom
    photodata_max -= amplitude * 0.01;
  } while (amplitude < 50);

  //------------- Filter Phototransistor readings -------------------------
  //replace ambiguous readings with the previous measurement
  if (newdata < (rolling_max - deviation) && newdata > (rolling_min + deviation)) {
    photodata[write_index] = photodata[SHIFT(write_index, -1)];
  } else photodata[write_index] = newdata;

  photodata_smooth = photodata_smooth * 0.2 + newdata * 0.8;

  if (photodata_smooth > photodata_average) data[write_index] = 1;
  else data[write_index] = 0;

  //correct for single flipped bits
  if (data[SHIFT(write_index, -2)] == data[write_index]) {
    data[SHIFT(write_index, -1)] = data[write_index];
  }


  //--------------- Convert to Bitstream ------------

  //calculate bit width
  static float period = 8;
  static uint8_t halfperiod = period / 2;
  static uint8_t bits = 1;
  static uint8_t width = 0;
  static uint8_t next_bit = period;
  static bool newbitFlag = false;

  //if edge detected
  if (data[SHIFT(write_index, -2)] != data[SHIFT(write_index, -1)]) {

    //write the first bit
    rx = (rx << 1) | (1 >> data[SHIFT(write_index, -1)]);  //invert and load next bit
    newbitFlag = true;
    next_bit = period + halfperiod;
    bits = 1;
    width = 0;
  }

  //if edge change not detected after period next bit is the same
  else if (width == next_bit) {
    digitalWrite(CENTER_PIN, HIGH);
    delay(1);
    rx = (rx << 1) | (1 >> data[SHIFT(write_index, -1)]);  //invert and load next bit
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
    Serial.println("data start");
    readData = true;
    rx = 0b0;  //clear RX
  } else if (readData) {
    static uint8_t current_bit = 0;
    static uint8_t current_byte = 0;

    data_buf[current_byte] |= (rx & 0b1) << current_bit;
    Serial.println(rx & 0b1);

    current_bit++;
    if (current_bit > 5) {
      //if 5th bit is 0, data is over, or read is corrupted
      if (!(rx & 0b1)) {
        //data read is finished, or data is corrupted
        readData = false;
        Serial.print("current byte: ");
        Serial.println(current_byte);
        //bytePos = 0;
        if ((current_byte + 1) % FRAME_WIDTH != 0) {
          Serial.println("PARTIAL FRAME");
          //return 0;	// not a full frame detected
        }
        if (current_bit != 6) {
          Serial.println("PARTIAL COLUMN");
          //return 0;	// didn't end on a full vertical line (minus the stop bit)
        }
        if ((current_byte + 1) / FRAME_WIDTH > MAX_FRAMES) {
          Serial.println("TOO MANY FRAMES");
          //return 0;	// too many frames
        }

        for (uint8_t i = 0; i < current_byte; i++) {
          Serial.println(data_buf[current_byte], HEX);
        }
        current_byte = 0;
        current_bit = 0;
      } else {
        current_byte++;
        current_bit = 0;
      }
    }
  }
}

  digitalWrite(LED_PIN, data[SHIFT(write_index, -1)]);
  /*
  Serial.print(bits); 
  Serial.print(",");
  Serial.print(width); 
  Serial.print(",");
  Serial.print(period); 
 //Serial.print(",");
  //Serial.print(rx, BIN); 
  Serial.println();
*/
  /*
  Serial.print(newdata);
  Serial.print(",");
  Serial.print(photodata_average);
  Serial.print(",");
  Serial.print(rolling_max); 
  Serial.print(",");
  Serial.print(rolling_min);
  Serial.print(",");
  Serial.print(photodata_smooth);
  Serial.print(",");
  Serial.print(photodata[write_index]); 
  Serial.print(",");
  Serial.print(data[SHIFT(write_index,-1)]*700);
  Serial.println();
*/

  write_index = SHIFT(write_index, 1);

  delay(11);
}