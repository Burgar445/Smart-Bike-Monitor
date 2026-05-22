#include <Arduino.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <avr/io.h>
#include <avr/interrupt.h>

float vitezaCurenta = 0.0;
float distantaTotala = 0.0;
bool magnetPrezent = false;
unsigned long timpUltimulUpdateLCD = 0;
unsigned long timpUltimaRotatie = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

const float CIRCUMFERINTA = 0.5;  // metri
const int PRAG_DETECTIE = 250;    // sub 250 detectăm magnetul
const int PRAG_ELIBERARE = 400;   // peste 400 considerăm că a plecat
float limitaViteza = 1.5;         // Limita  viteza

volatile unsigned long num_toggles = 0;
volatile unsigned long max_toggles = 0;

bool stareButonAnterioara = true; // true = HIGH
unsigned long timpUltimaApasare = 0;

// Întreruperea hardware pentru buzzer
ISR(TIMER2_COMPA_vect) {
  PORTD ^= (1 << PORTD4);
  num_toggles++;
  
  if (num_toggles >= max_toggles) {
    TIMSK2 &= ~(1 << OCIE2A);
    PORTD &= ~(1 << PORTD4);
  }
}

void buzzer(uint16_t freq_hz, uint16_t duration_ms) {
  if (freq_hz == 0) return;

  max_toggles = (2UL * freq_hz * duration_ms) / 1000UL;
  num_toggles = 0;

  uint32_t ocr_val = (16000000UL / (2UL * 128UL * freq_hz)) - 1;
  if (ocr_val > 255) ocr_val = 255; 

  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;

  OCR2A = (uint8_t)ocr_val;
  TCCR2A |= (1 << WGM21);
  TCCR2B |= (1 << CS22) | (1 << CS20);
  TIMSK2 |= (1 << OCIE2A);
}

void actualizeazaLCD() {
  if (millis() - timpUltimulUpdateLCD > 500 || timpUltimulUpdateLCD == 0) {
    timpUltimulUpdateLCD = millis();
    
    lcd.setCursor(0, 0);
    lcd.print("V: ");
    lcd.print(vitezaCurenta, 1); 
    lcd.print(" km/h    "); 

    lcd.setCursor(0, 1);
    lcd.print("D: ");
    if (distantaTotala < 1000) {
      lcd.print(distantaTotala, 1);
      lcd.print(" m      ");
    } else {
      lcd.print(distantaTotala / 1000.0, 2);
      lcd.print(" km     ");
    }
  }
}

void setup() {
  // 1. Initializare LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Bike Computer");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
  delay(1500);
  lcd.clear();

  // 2. Configurare pentru LED (PD7) și Buzzer (PD4) ca IEȘIRI
  DDRD |= (1 << DDD7); 
  DDRD |= (1 << DDD4);

  // 3. Configurare ADC pentru Senzorul Hall (PC0 / A0)
  ADMUX = (1 << REFS0);
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // Activare ADC + Prescaler 128

  // 4. Configurare Buton (SW0) pe PB7
  DDRB &= ~(1 << DDB7);   // Setează PB7 ca intrare (0)
  PORTB |= (1 << PORTB7); // Activează rezistența internă de PULL-UP pe PB7
}

void loop() {
  // --- CITIRE SENZOR
  ADCSRA |= (1 << ADSC);          
  while (ADCSRA & (1 << ADSC));   
  int citireADC = ADC;            

  unsigned long timpAcum = millis();

  // --- LOGICĂ VITEZOMETRU ---
  if (citireADC < PRAG_DETECTIE && !magnetPrezent) {
    magnetPrezent = true;
    unsigned long deltaT = timpAcum - timpUltimaRotatie;

    if (deltaT > 100) { // Debounce
      vitezaCurenta = (CIRCUMFERINTA / (deltaT / 1000.0)) * 3.6;
      distantaTotala += CIRCUMFERINTA;
      timpUltimaRotatie = timpAcum;
      buzzer(2000, 20);
    }
  } 
  else if (citireADC > PRAG_ELIBERARE && magnetPrezent) {
    magnetPrezent = false;
  }

  if (timpAcum - timpUltimaRotatie > 3000) {
    vitezaCurenta = 0.0;
  }
  actualizeazaLCD();

  static unsigned long timpAlerta = 0;
  static unsigned long timpBlink = 0;
  static bool stareAlerta = false;

  if (vitezaCurenta > limitaViteza) {
    if (millis() - timpAlerta > 1000) { 
      buzzer(500, 100);
      timpAlerta = millis();
    }
    
    if (millis() - timpBlink > 200) {
      timpBlink = millis();
      stareAlerta = !stareAlerta; 
      
      if (stareAlerta) {
        PORTD |= (1 << PORTD7);  // Aprinde LED
        buzzer(1000, 50); 
      } else {
        PORTD &= ~(1 << PORTD7); // Stinge LED
      }
    }
  } else {
    PORTD &= ~(1 << PORTD7);     
  }

  // returnează o valoare diferită de 0 dacă pinul e HIGH
  bool stareButon = (PINB & (1 << PINB7)) ? true : false; 

  // Detectăm apăsarea
  if (stareButon == false && stareButonAnterioara == true) {
    // Debounce
    if (timpAcum - timpUltimaApasare > 250) {
      distantaTotala = 0.0;
      timpUltimaApasare = timpAcum;   
      buzzer(1500, 300);
      timpUltimulUpdateLCD = 0;// Forțăm ecranul să se actualizeze instant
    }
  }
  stareButonAnterioara = stareButon; // Salvăm starea pentru următoarea iterație
}