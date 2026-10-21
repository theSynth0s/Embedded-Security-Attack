// Bit-bang serial bridge + clock generator for a slowed-down ATmega328 target
// Arduino Uno (ATmega328P)
//
//  USB serial <-> software UART (8N1, LSB first, idle HIGH) on pins 8 (RX) / 9 (TX)
//  Clock for the target: 50 kHz (so its "9600 baud" becomes ~60 baud, ~16.5 ms per bit)
//
// What makes the RX robust:
//   * every line transition is timestamped in a pin-change ISR (no polling jitter)
//   * pulses shorter than 1/16 bit are discarded (noise / crosstalk from the clock wire)
//   * the start bit is re-checked at its centre; the stop bit is validated (framing error
//     -> wait for a full idle bit before listening again)
//   * every edge inside a frame re-aligns the sampling grid (hard sync) AND corrects the
//     bit-period estimate, so the receiver follows the real speed of the target instead of
//     trusting a fixed constant. TX uses the same measured period.
//
// Clock generation: the old 100 kHz interrupt used ~60 % of the CPU and made other
// interrupts late (so clock pulses were lost whenever the Arduino was busy -> the target's
// real baud rate wandered). Two better options, see CLOCK_HW_PIN10.

#include "HardwareSerial.h"
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>

// ============================== CONFIG ==============================
// 0 : clock on pin 7, toggled by a 2-instruction interrupt (no wiring change)
// 1 : clock on pin 10 straight from Timer1 hardware (OC1B): no interrupt at all,
//     perfectly regular. Move the clock wire from pin 7 to pin 10.
#define CLOCK_HW_PIN10     1
#define TARGET_CLOCK_FREQ  50000UL    // Hz delivered to the target

// Initial guess for the target's bit period (us). It is refined automatically while receiving.
//const unsigned long BIT_NOMINAL_US = 16480UL; // without capacitors
const unsigned long BIT_NOMINAL_US = 16480UL; // with capacitors

// 1: TX uses the bit period measured on RX (same clock domain as the target), 0: always nominal
#define TX_FOLLOWS_RX      1
// ====================================================================

const uint8_t PIN_RX = 8;             // PB0 = PCINT0
const uint8_t PIN_TX = 9;
#define RX_PIN_MASK _BV(PB0)

// The measured period is kept inside +/-20 % of the nominal one
const unsigned long BIT_MIN_US = BIT_NOMINAL_US * 4UL / 5UL;
const unsigned long BIT_MAX_US = BIT_NOMINAL_US * 6UL / 5UL;

// Pulses shorter than this are noise (~1 ms). Must stay far below one bit period.
const unsigned long GLITCH_US  = BIT_NOMINAL_US / 16;

// ============================== CLOCK ===============================
// Timer1 in CTC mode: one period = F_CPU / (2 * freq) cycles = 160 cycles = 10 us,
// the output toggles once per period -> 50 kHz square wave.
const uint16_t CLOCK_TOP = (uint16_t)(F_CPU / (2UL * TARGET_CLOCK_FREQ) - 1);

volatile uint32_t clock_pulse = 0; // uint16_t : 65535, 32 : 4294967295
uint32_t clock_capture = 0;

#if !CLOCK_HW_PIN10 && defined(__AVR__)
// Naked ISR: no prologue/epilogue, nothing is saved, nothing is clobbered.
// SBI on PIND toggles PD7 (pin 7); RETI returns. ~10 cycles instead of ~100.
ISR(TIMER1_COMPA_vect, ISR_NAKED) {
  asm volatile(
    "sbi %0, %1 \n\t"
    "reti       \n\t"
    :: "I"(_SFR_IO_ADDR(PIND)), "I"(PD7));
}
#else
ISR(TIMER1_COMPA_vect) {
  clock_pulse++;
}
#endif

static void startTargetClock() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = CLOCK_TOP;
#if CLOCK_HW_PIN10
  pinMode(10, OUTPUT);
  OCR1B  = CLOCK_TOP / 2;
  TCCR1A = _BV(COM1B0);                 // toggle OC1B (pin 10) on compare match B
  TCCR1B = _BV(WGM12) | _BV(CS10);      // CTC (TOP = OCR1A), no prescaler
  TIMSK1 |= _BV(OCIE1A); // autorize ISR
#else
  pinMode(7, OUTPUT);
  TCCR1B = _BV(WGM12) | _BV(CS10);      // CTC (TOP = OCR1A), no prescaler
  TIMSK1 |= _BV(OCIE1A); // autorize ISR
#endif
  interrupts();
}

// ===================== EDGE CAPTURE (pin-change ISR) =====================
const uint8_t EDGE_Q_SIZE = 16;         // must be a power of two
volatile unsigned long edgeTime[EDGE_Q_SIZE];
volatile uint8_t edgeLevel[EDGE_Q_SIZE];
volatile uint8_t edgeHead = 0;
volatile uint8_t edgeTail = 0;
volatile uint8_t isrLastLevel = 1;

ISR(PCINT0_vect) {
  unsigned long now = micros();
  uint8_t level = (PINB & RX_PIN_MASK) ? 1 : 0;
  if (level == isrLastLevel) return;          // duplicate event
  isrLastLevel = level;
  uint8_t next = (edgeHead + 1) & (EDGE_Q_SIZE - 1);
  if (next != edgeTail) {                      // drop if queue is full
    edgeTime[edgeHead]  = now;
    edgeLevel[edgeHead] = level;
    edgeHead = next;
  }
}

// Returns the next edge that has proven to be real, with its original timestamp.
//  - two edges closer than GLITCH_US form a noise pulse: both are dropped
//  - the newest edge is only released once it has been stable for GLITCH_US
static bool popEdge(unsigned long &t, uint8_t &lvl, unsigned long now) {
  for (;;) {
    if (edgeTail == edgeHead) return false;
    uint8_t second = (edgeTail + 1) & (EDGE_Q_SIZE - 1);
    unsigned long t1 = edgeTime[edgeTail];
    if (second != edgeHead) {
      if (edgeTime[second] - t1 < GLITCH_US) {          // short pulse: drop both edges
        edgeTail = (second + 1) & (EDGE_Q_SIZE - 1);
        continue;
      }
    } else if ((long)(now - t1) < (long)GLITCH_US) {    // newest edge: not confirmed yet
      return false;
    }
    t   = t1;
    lvl = edgeLevel[edgeTail];
    edgeTail = second;
    return true;
  }
}

static inline bool edgePending() { return edgeTail != edgeHead; }

// ========================== RX STATE MACHINE ==========================
// RX_WAIT_IDLE : line state unknown / after an error -> wait until it has been HIGH for 1 bit
// RX_IDLE      : waiting for a falling edge (start bit)
// RX_RECEIVING : frame in progress; rxBit = next bit to sample
//                0 = start, 1..8 = data bits (LSB first), 9 = stop
enum RxState : uint8_t { RX_WAIT_IDLE, RX_IDLE, RX_RECEIVING };
RxState rxState = RX_WAIT_IDLE;

unsigned long rxBitUs = BIT_NOMINAL_US;   // current estimate of the target's bit period
unsigned long rxFrameStart = 0;           // estimated time of the start bit's falling edge
uint8_t  rxBit = 0;
uint8_t  rxBuffer = 0;
unsigned long rxIdleSince = 0;
bool     rxIdleValid = false;

volatile bool rxDataReady = false;
uint8_t  rxByte = 0;
uint16_t rxFramingErrors = 0;

static inline unsigned long rxSampleTime(uint8_t bit) {
  return rxFrameStart + (unsigned long)bit * rxBitUs + rxBitUs / 2;   // centre of the bit
}

static void rxTakeSample(uint8_t level) {
  if (rxBit == 0) {
    // Start bit must still be LOW at its centre, otherwise it was a glitch
    if (level) { rxState = RX_IDLE; return; }
  } else if (rxBit <= 8) {
    if (level) rxBuffer |= (1 << (rxBit - 1));
  } else {
    // Stop bit
    if (level) {
      rxByte = rxBuffer;
      rxDataReady = true;
      rxState = RX_IDLE;
    } else {
      // Framing error: we were probably out of sync -> wait for a clean idle line
      rxFramingErrors++;
      rxIdleValid = false;
      rxState = RX_WAIT_IDLE;
    }
    return;
  }
  rxBit++;
}

// Take every sample that was due up to time t, knowing the line level over that interval
static void rxConsumeSamples(unsigned long t, uint8_t level) {
  while (rxState == RX_RECEIVING && (long)(t - rxSampleTime(rxBit)) >= 0) {
    rxTakeSample(level);
  }
}

static void rxHandleEdge(unsigned long t, uint8_t lvl) {
  switch (rxState) {
    case RX_WAIT_IDLE:
      if (lvl) { rxIdleSince = t; rxIdleValid = true; }
      else     { rxIdleValid = false; }
      break;

    case RX_IDLE:
      if (!lvl) {                       // falling edge = start bit
        if(clock_capture == 0) {clock_capture = clock_pulse;}
        //clock_capture = clock_pulse; // FIRST
        rxFrameStart = t;
        rxBit = 0;
        rxBuffer = 0;
        rxState = RX_RECEIVING;
      }
      break;

    case RX_RECEIVING: {
      // Samples due *before* this edge saw the previous level (= opposite of the new one)
      rxConsumeSamples(t, !lvl);
      if (rxState != RX_RECEIVING) {    // frame just ended: this edge belongs to the idle logic
        rxHandleEdge(t, lvl);
        break;
      }
      // This edge is a bit boundary: find which one, then re-align the grid and the period.
      long T  = (long)rxBitUs;
      long dt = (long)(t - rxFrameStart);
      if (dt > 0) {
        long n   = (dt + T / 2) / T;                 // nearest boundary index
        long err = dt - n * T;                       // + = edge later than expected
        if (n >= 1 && n <= 9 && err <= T / 4 && err >= -(T / 4)) {
          long newT = T + err / 8;                   // frequency correction
          if (newT < (long)BIT_MIN_US) newT = BIT_MIN_US;
          if (newT > (long)BIT_MAX_US) newT = BIT_MAX_US;
          rxBitUs = (unsigned long)newT;
          rxFrameStart = t - (unsigned long)n * rxBitUs;   // anchor the grid on this edge
        }
      }
      break;
    }
  }
}

void rxService() {
  unsigned long t;
  uint8_t lvl;
  unsigned long now = micros();
  while (popEdge(t, lvl, now)) rxHandleEdge(t, lvl);

  // An edge that is still being confirmed (or a glitch in progress): wait, so that the
  // pin level read below can never belong to an unprocessed edge.
  if (edgePending()) return;

  switch (rxState) {
    case RX_RECEIVING: {
      uint8_t level = (PINB & RX_PIN_MASK) ? 1 : 0;
      rxConsumeSamples(now, level);
      break;
    }
    case RX_WAIT_IDLE:
      if (rxIdleValid && (now - rxIdleSince) >= rxBitUs) rxState = RX_IDLE;
      break;
    default:
      break;
  }
}

// ========================== TX STATE MACHINE ==========================
enum TxState { TX_IDLE, TX_START_BIT, TX_DATA_BITS, TX_STOP_BIT };
TxState txState = TX_IDLE;

unsigned long txTimer = 0;
unsigned long txBitUs = BIT_NOMINAL_US;
uint8_t txBuffer = 0;
int txBitIndex = 0;
bool txInProgress = false;

void startBitBangTx(uint8_t data) {
  if (!txInProgress) {
    txBuffer = data;
    txBitIndex = 0;
    txInProgress = true;
    txState = TX_START_BIT;
#if TX_FOLLOWS_RX
    txBitUs = rxBitUs;                  // fixed for the whole frame
#else
    txBitUs = BIT_NOMINAL_US;
#endif
    digitalWrite(PIN_TX, LOW);          // start bit
    txTimer = micros();
  }
}

void handleTxStateMachine() {
  if (!txInProgress) return;
  if (micros() - txTimer < txBitUs) return;
  txTimer += txBitUs;                   // fixed schedule: no cumulative drift

  switch (txState) {
    case TX_START_BIT:
      txState = TX_DATA_BITS;
      digitalWrite(PIN_TX, (txBuffer >> txBitIndex) & 0x01);
      break;

    case TX_DATA_BITS:
      txBitIndex++;
      if (txBitIndex < 8) {
        digitalWrite(PIN_TX, (txBuffer >> txBitIndex) & 0x01);
      } else {
        txState = TX_STOP_BIT;
        digitalWrite(PIN_TX, HIGH);     // stop bit
      }
      break;

    case TX_STOP_BIT:
      txInProgress = false;
      txState = TX_IDLE;
      break;

    default:
      break;
  }
}

// ============================ SETUP / LOOP ============================
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(12, INPUT);

  pinMode(PIN_TX, OUTPUT);
  digitalWrite(PIN_TX, HIGH);           // idle = HIGH

  pinMode(PIN_RX, INPUT_PULLUP);        // idle HIGH even if nothing is connected
  isrLastLevel = (PINB & RX_PIN_MASK) ? 1 : 0;
  rxIdleSince = micros();
  rxIdleValid = isrLastLevel;           // boot: require one idle bit before trusting a start edge
  rxState = RX_WAIT_IDLE;
  PCIFR |= _BV(PCIF0);                  // clear stale flag
  PCMSK0 |= _BV(PCINT0);                // only pin 8 (PB0), not the TX pin
  PCICR  |= _BV(PCIE0);

  startTargetClock();

  Serial.begin(115200);
  while (!Serial);
  Serial.println("--- Pont Serie Bit-Bang Non-Bloquant Actif ---");
}

uint32_t time_start = 0;
uint32_t time_stop = 0;
bool previous_state = false;

void loop() {
  rxService();
  handleTxStateMachine();

  // USB -> software TX
  if (Serial.available() > 0 && !txInProgress) {
    startBitBangTx(Serial.read());
  }

  // Software RX -> USB
  if (rxDataReady) {
    //clock_capture = clock_pulse; // FIRST
    Serial.write(rxByte);
    rxDataReady = false;
  }

  // timing part
  if (previous_state == false && digitalRead(12) == true) {
    clock_pulse = 0; // FIRST
    clock_capture = 0; // FIRST
    previous_state = true;
    //time_start = millis();
    
  }
  else if (previous_state == true && digitalRead(12) == false) {
    //clock_capture = clock_pulse; // FIRST
    previous_state = false;
    //time_stop = millis();

    /*Serial.print("\n(chronos : ");
    Serial.print(time_stop-time_start);
    Serial.print(")\n");*/
    Serial.print("\n(clocks=");
    //Serial.print(clock_pulse);
    Serial.print(clock_capture);
    Serial.print(")\n");
  }

  /*if (previous_state == true && clock_capture != 0) {
    Serial.print("\n(clock_capture : ");
    Serial.print(clock_capture);
    Serial.print(")\n");
    clock_capture = 0;
  }*/
}
