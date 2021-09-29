#include <IRremote.h>
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))


#define PIN_UNDERVOLTAGE_LED 8
#define PIN_IR_LED 9
#define PIN_MINUS_LED 10 // for simulation, to see when sending volume-
#define PIN_PLUS_LED 11 // for simulation, to see when sending volume+

#define PIN_AUDIO A1
#define PIN_DEADBAND A2
#define PIN_REACT A3
#define PIN_UNDERVOLTAGE A7


#define IR_DATA_VOL_PLUS 0x3434E817
#define IR_DATA_VOL_MINUS 0x34346897

int deadband;
int react;
int audio;
byte audiocounter = 0;
byte loudercounter = 0;
byte volcounter = 0;
byte timercounter = 0;
byte timercountervol = 0;
byte silencecounter = 0;
bool trig = 0;
bool lowertrig = 0;
bool lowertrigtimer = 0;
bool volup = 0;
byte i = 0;


void setup() {
  Serial.begin(250000);
  sbi(ADCSRA, ADPS2);
  cbi(ADCSRA, ADPS1);
  cbi(ADCSRA, ADPS0);
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= (1 << CS12) | (1 << WGM12);
  TIMSK1 |= (1 << OCIE1A);
  OCR1A = 31250;
  TCCR2A = 0;
  TCCR2B = 0;
  TCCR2A |= (1 << WGM21);
  TCCR2B |= (1 << CS20) | (1 << CS21) | (1 << CS22) ;
  TIMSK2 |= (1 << OCIE2A);

  IrSender.begin(PIN_IR_LED, DISABLE_LED_FEEDBACK); //IR LED
  pinMode(PIN_UNDERVOLTAGE_LED, OUTPUT); //Status
  digitalWrite(PIN_UNDERVOLTAGE_LED, LOW);

  pinMode(PIN_AUDIO, INPUT); //Audio
  pinMode(PIN_DEADBAND, INPUT); //Deadband
  pinMode(PIN_REACT, INPUT); //React
  pinMode(PIN_UNDERVOLTAGE, INPUT); //undervoltage

  pinMode(PIN_MINUS_LED,OUTPUT);
  pinMode(PIN_PLUS_LED,OUTPUT);
  digitalWrite(PIN_MINUS_LED,LOW);
  digitalWrite(PIN_PLUS_LED,LOW);


  deadband = map(analogRead(PIN_DEADBAND), 0, 1023, 511, 0);
  OCR2A = map(analogRead(PIN_REACT), 1023, 0, 255, 127);
}

void loop() {

  Serial.print(0);
  Serial.print(" ");
  Serial.print(1023);
  Serial.print(" ");
  Serial.print(analogRead(PIN_AUDIO));
  Serial.print(" ");
  Serial.print(511 + deadband);
  Serial.print(" ");
  Serial.println(511 - deadband);

  if (audiocounter > 10) {
    lowertrig = 1;
    audiocounter = 0;
  }

  if (loudercounter > 20) {
    while (i < volcounter) {
      IrSender.sendSAMSUNG(IR_DATA_VOL_PLUS, 32); // Vol+
      digitalWrite(PIN_PLUS_LED,HIGH);
      i++;
      delay(200);
      digitalWrite(PIN_PLUS_LED,LOW);
    }
    volup = 0;
    i = 0;
    volcounter = 0;
    loudercounter = 0;
  }

  audio = analogRead(PIN_AUDIO);

  if ((volup == 1) && (audio < 540) && (audio > 480) && (trig == 0)) {
    TCNT2 = 0;
    trig = 1;
  }

  if ((volup == 1) && ((audio > 540) || (audio < 480))){
    loudercounter = 0;
  }

  if ((audio > (511 + deadband)) || (audio < (511 - deadband))) {
    if ((trig == 0) && (lowertrig == 0) && (volup == 0)) {
      TCNT2 = 0;
      trig = 1;
    }
    if (lowertrig == 1) {
      volcounter++;
      IrSender.sendSAMSUNG(IR_DATA_VOL_MINUS, 32); //Vol-
      digitalWrite(PIN_MINUS_LED,HIGH);
      delay(200);
      digitalWrite(PIN_MINUS_LED,LOW);
      lowertrigtimer = 1;
    }
  }
}

ISR(TIMER1_COMPA_vect) {
  deadband = map(analogRead(PIN_DEADBAND), 0, 1023, 511, 0);
  OCR2A = map(analogRead(PIN_REACT), 1023, 0, 255, 127); //200ms - 100ms
  digitalWrite(PIN_UNDERVOLTAGE_LED, (analogRead(PIN_UNDERVOLTAGE) < 540));
}

ISR(TIMER2_COMPA_vect) {
  if (trig == 1) {
    timercounter++;
    silencecounter = 0;
  }
  if ((timercounter > 6) && (volup == 0)) {
    audiocounter++;
    timercounter = 0;
    trig = 0;
  }

  if ((timercounter > 6) && (volup == 1)) {
    loudercounter++;
    timercounter = 0;
    trig = 0;
  }

  if ((audiocounter > 0) && (trig == 0) ) {
    silencecounter++;
    if (silencecounter > 50) {
      audiocounter = 0;
      silencecounter = 0;
    }
  }
  if (lowertrig == 1) {
    if (lowertrigtimer == 0) {
      timercountervol++;
    }
    if (lowertrigtimer == 1) {
      timercountervol = 0;
      lowertrigtimer = 0;
    }
    if (timercountervol > 100) {
      volup = 1;
      lowertrig = 0;
    }
  }
}
