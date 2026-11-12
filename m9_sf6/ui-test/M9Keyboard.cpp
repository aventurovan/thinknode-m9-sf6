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
  xTaskCreatePinnedToCore(kbTask, "m9kb", 4096, NULL, 3, &kb_task, 1);
  attachInterrupt(digitalPinToInterrupt(M9KB_PIN_INT), kbIsr, RISING);
}

int M9Keyboard::readReg(uint8_t reg) {
  if (_wire == NULL) return -1;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) { _errors++; return -1; }
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

void M9Keyboard::taskLoop() {
  if (_addr == 0) {                               // klawiatury nie ma: szukamy co 2 s
    vTaskDelay(pdMS_TO_TICKS(2000));
    find();
    return;
  }
  // czekamy na przerwanie (bez odpytywania: to ono powodowało „samoczynne” powtarzanie Entera)
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) return;

  Press p;
  // od razu: najpierw rejestr 0x01 osobno (tak czyta klawisz V2 w innych projektach), potem 0x00–0x07
  int first01 = readReg(0x01);
  snapshot(p.r[0]);
  vTaskDelay(pdMS_TO_TICKS(20));
  snapshot(p.r[1]);
  vTaskDelay(pdMS_TO_TICKS(80));
  snapshot(p.r[2]);

  // klawisz: 0x05, potem 0x01 (o ile nie jest to stała 0x03), potem dowolny rejestr, który zmienił się względem spoczynku
  int k = 0;
  if (isKey(p.r[0][5]) && p.r[0][5] != p.r[2][5]) k = p.r[0][5];
  else if (isKey(first01) && first01 != 0x03) k = first01;
  else if (isKey(p.r[0][5]) && p.r[0][5] != 0xB8) k = p.r[0][5];
  else {
    for (int i = 0; i < M9KB_SNAP_REGS && !k; i++)
      if (isKey(p.r[0][i]) && p.r[0][i] != p.r[2][i]) k = p.r[0][i];
  }
  p.key = (uint8_t)k;

  portENTER_CRITICAL(&_mux);
  if (_press_len == 4) { memmove(&_press[0], &_press[1], sizeof(Press) * 3); _press_len = 3; }
  _press[_press_len] = p;
  _press_len = _press_len + 1;
  if (k) push((uint8_t)k);
  portEXIT_CRITICAL(&_mux);
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
