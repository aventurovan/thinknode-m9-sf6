#include "M9Keyboard.h"
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>

static volatile uint32_t kb_irq_count = 0;
static TaskHandle_t kb_task = NULL;
static M9Keyboard* kb_self = NULL;

static void IRAM_ATTR kbIsr() {
  kb_irq_count++;
  BaseType_t woken = pdFALSE;
  if (kb_task) vTaskNotifyGiveFromISR(kb_task, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void kbTask(void*) {
  for (;;) kb_self->taskLoop();
}

bool M9Keyboard::probe(TwoWire* w, uint8_t a) {
  w->beginTransmission(a);
  return w->endTransmission() == 0;
}

// szukamy tylko pod dwoma adresami klawiatury (pełny skan magistrali wydłużał start)
void M9Keyboard::find() {
  TwoWire* buses[2] = { &Wire1, &Wire };
  const uint8_t addrs[2] = { M9KB_ADDR_V2, M9KB_ADDR_V1 };
  for (int b = 0; b < 2; b++)
    for (int i = 0; i < 2; i++)
      if (probe(buses[b], addrs[i])) {
        _wire = buses[b]; _bus = b == 0 ? 1 : 0; _addr = addrs[i];
        _hw = readReg(0x00);
        _fw = readReg(0xFE);
        for (int n = 0; n < 4; n++) { int k = readReg(0x01); if (k <= 0 || k == M9K_NONE || k == 0xFF) break; }   // zdejmij stary klawisz
        return;
      }
}

void M9Keyboard::begin() {
  pinMode(M9KB_PIN_INT, INPUT);
  pinMode(M9KB_PIN_LED, OUTPUT);
  setBacklight(true);

  REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  Wire1.begin(M9KB_BUS1_SDA, M9KB_BUS1_SCL, 100000);
  Wire1.setTimeOut(20);
  find();

  kb_self = this;
  attachInterrupt(digitalPinToInterrupt(M9KB_PIN_INT), kbIsr, RISING);
  xTaskCreatePinnedToCore(kbTask, "m9kb", 4096, NULL, 3, &kb_task, 1);
}

// Numer rejestru wysyłamy jako osobną, zakończoną transmisję (STOP), dopiero potem czytamy bajt.
// Kontroler nie odpowiada poprawnie na „powtórzony start” — przez to wcześniejsze wersje czytały przesunięte wartości.
int M9Keyboard::readReg(uint8_t reg) {
  if (_wire == NULL) return -1;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(true) != 0) { _errors++; return -1; }
  if (_wire->requestFrom((int)_addr, 1) != 1) { _errors++; return -1; }
  return _wire->read();
}

void M9Keyboard::snapshot(int16_t* dest) {
  for (int i = 0; i < M9KB_SNAP_REGS; i++) dest[i] = readReg(i);
}

void M9Keyboard::push(uint8_t k) {
  uint8_t next = (_q_tail + 1) & 15;
  if (next != _q_head) { _queue[_q_tail] = k; _q_tail = next; }
}

static bool isKey(int k) { return k > 0 && k != 0xFF; }

// Protokół (potwierdzony na sprzęcie w otwartym projekcie Launcher, boards/elecrow-thinknode-m9/connections.md):
//  - rejestr 0x01 trzyma jeden oczekujący klawisz aż do odczytu; po odczycie zwraca 0x88 („brak klawisza”),
//  - linia KB_INT to tylko „budzenie” i pojawia się dla nielicznych klawiszy (np. Enter), więc rejestr trzeba odpytywać,
//  - rejestr 0x05 nie jest rejestrem klawisza (zwraca 0xFF).
void M9Keyboard::taskLoop() {
  if (_addr == 0) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    find();
    return;
  }
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));     // przerwanie tylko skraca czekanie

  for (int n = 0; n < 4; n++) {                    // kilka pod rząd, gdyby czekało więcej klawiszy
    int k = readReg(0x01);
    if (k <= 0 || k == M9K_NONE || k == 0xFF) break;
    portENTER_CRITICAL(&_mux);
    push((uint8_t)k);
    portEXIT_CRITICAL(&_mux);
  }
}

uint8_t M9Keyboard::readKey() {
  if (_q_head == _q_tail) return 0;
  uint8_t k = _queue[_q_head];
  _q_head = (_q_head + 1) & 15;
  return k;
}

void M9Keyboard::getPress(int i, Press* out) {
  portENTER_CRITICAL(&_mux);
  *out = _press[_press_len - 1 - i];
  portEXIT_CRITICAL(&_mux);
}

uint32_t M9Keyboard::irqCount() const { return kb_irq_count; }
