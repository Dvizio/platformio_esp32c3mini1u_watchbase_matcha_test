#define LGFX_USE_V1
#include <WiFi.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Ticker.h>
#include "CST816D.h"
#include "ui.h"
#include "I2C_BM8563.h"

// --- PIN DEFINITIONS ---
#define I2C_SDA 4
#define I2C_SCL 5
#define TP_INT 0
#define TP_RST 1     // Fixed from factory code
#define PWR_EN_PIN 3 // The "Secret" Factory Power Gate

// Encoder (Moved from Pin 8 to avoid boot hang)
// #define ENCODER_A_PIN 19
// #define ENCODER_B_PIN 18
// #define SWITCH_PIN 20

int currentStepperIndex = 0;      // Global variable to track stepper value
int stepperVal[3] = {20, 50, 50}; // Array to hold values for 3 steppers

// --- DISPLAY CONFIG ---
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void)
  {
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 80000000;
    cfg.pin_sclk = 6;
    cfg.pin_mosi = 7;
    cfg.pin_dc = 2;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto p_cfg = _panel_instance.config();
    p_cfg.pin_cs = 10;
    p_cfg.panel_width = 240;
    p_cfg.panel_height = 240;
    p_cfg.invert = true;
    _panel_instance.config(p_cfg);
    setPanel(&_panel_instance);
  }
};

LGFX tft;
CST816D touch(I2C_SDA, I2C_SCL, TP_RST, TP_INT);
I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire);

// --- LVGL BUFFERS ---
static const uint32_t screenWidth = 240;
static const uint32_t screenHeight = 240;
#define buf_size 40 // Increased slightly for smoother UI
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

// --- DISPLAY FLUSH ---
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  if (tft.getStartCount() == 0)
    tft.endWrite();
  tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::swap565_t *)&color_p->full);
  lv_disp_flush_ready(disp);
}

// --- TOUCH READ ---
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  uint16_t touchX, touchY;
  uint8_t gesture;
  if (touch.getTouch(&touchX, &touchY, &gesture))
  {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}
extern "C"
{
  void startButtonListener(lv_event_t *e)
  {
    Serial.println("Start button pressed!");
  }

  void stopButtonListener(lv_event_t *e)
  {
    Serial.println("Stop button pressed!");
  }

  void changestepper(lv_event_t *e)
  {
    currentStepperIndex = (currentStepperIndex + 1) % 3; // Cycle through 0, 1, 2
    lv_label_set_text_fmt(ui_stepper1Label, "Stepper%d: %d", currentStepperIndex + 1, stepperVal[currentStepperIndex]);
    Serial.println("Stepper1 button pressed!");
  }

  void decrementDown(lv_event_t *e)
  {
    Serial.println("Stepper1 down button pressed!");
    stepperVal[currentStepperIndex] = stepperVal[currentStepperIndex] > 0 ? stepperVal[currentStepperIndex] - 1 : 0; // Decrement with floor at 0
    lv_label_set_text_fmt(ui_stepper1Label, "Stepper%d: %d", currentStepperIndex + 1, stepperVal[currentStepperIndex]);
  }

  void incrementUp(lv_event_t *e)
  {
    Serial.println("Stepper1 up button pressed!");
    stepperVal[currentStepperIndex] = stepperVal[currentStepperIndex] < 100 ? stepperVal[currentStepperIndex] + 1 : 100; // Increment with ceiling at 100
    lv_label_set_text_fmt(ui_stepper1Label, "Stepper%d: %d", currentStepperIndex + 1, stepperVal[currentStepperIndex]);
  }
}
void setup()
{
  // 1. IMMEDIATE POWER ON
  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);
  delay(200);

  Serial.begin(115200);
  Serial.println("System Booting with Factory Power Specs...");

  // 2. I2C & SENSORS
  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  rtc.begin();
  touch.begin();

  // 3. LVGL & DISPLAY
  lv_init();
  tft.init();
  tft.initDMA();

  // Allocation with check to prevent memory hang
  buf1 = (lv_color_t *)heap_caps_malloc(screenWidth * buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = (lv_color_t *)heap_caps_malloc(screenWidth * buf_size * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, screenWidth * buf_size);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // 4. UI START
  ui_init();
  Serial.println("Setup Finished!");
}

void loop()
{
  lv_timer_handler();
  delay(5);
}