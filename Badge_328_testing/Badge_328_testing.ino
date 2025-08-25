#define PHOTODIODE A0
#define LED_PIN 7

#define DATALENGTH 8
#define SHIFT(cur, shift) (cur + shift) & (DATALENGTH - 1)

//uint16_t data[DATALENGTH];
uint8_t read_index = 0;
uint8_t write_index = 0;

void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(PHOTODIODE, INPUT_PULLUP);
}

void loop(){
  static float photodiode_average = 0;
  static uint16_t photodiode_max = 0;

  static uint16_t photodata[DATALENGTH];
  static bool data[DATALENGTH];
  static uint8_t rx = 0;

  pinMode(PHOTODIODE, INPUT_PULLUP);
  delayMicroseconds(200);
  pinMode(PHOTODIODE, INPUT);
  delayMicroseconds(500);

  photodata[write_index] = analogRead(PHOTODIODE);

  //load new bit into array
  if(photodata[write_index] >= (photodiode_max >> 1) + (photodiode_max >> 3)){
    data[write_index] = 1;
    //photodiode_max = (photodiode_max * 0.93 + photodata[write_index] * 0.07 );
    if(photodata[write_index] > photodiode_max) photodiode_max = photodata[write_index];
  }
  else data[write_index] = 0;

  //correct data from screen errors
  if(data[SHIFT(write_index, -2)] == data[write_index]){
    data[SHIFT(write_index, -1)] = data[write_index];
    photodata[SHIFT(write_index, -1)] = photodata[write_index];
  }

  static uint8_t edge_counter = 0;
  static uint8_t period = 8;
  static uint8_t sample_counter = 0;

  //if edge detected
  if (data[SHIFT(write_index, -2) != data[SHIFT(write_index, -1)]){
    edge_counter++;
    period = sample_counter;
    sample_counter = 1;
  }
  else{
    sample_counter++;
    if ( (sample_counter >> 2) == (period >> 2) )
  }

  /*
  keep track of pulse centers
  sample from estimated center
  */


  digitalWrite(LED_PIN, data[SHIFT(write_index, -1)]);
  

 
  //Serial.println(data[SHIFT(write_index,-1)]);
  Serial.print(data[SHIFT(write_index, -1)]); 
  Serial.print(",");
  Serial.print(photodiode_max); 
  Serial.print(",");
  Serial.print(photodata[write_index]);
  Serial.println();
  //write_index = (write_index + 1) & ~DATALENGTH;
  write_index = SHIFT(write_index, 1);







  delay(11);
}