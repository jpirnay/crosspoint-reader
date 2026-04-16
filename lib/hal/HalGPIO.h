#pragma once

#include <Arduino.h>
#include <InputManager.h>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

// Xteink X3 Hardware
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

// TI BQ27220 Fuel gauge I2C
#define I2C_ADDR_BQ27220 0x55  // Fuel gauge I2C address
#define BQ27220_SOC_REG 0x2C   // StateOfCharge() command code (%)
#define BQ27220_CUR_REG 0x0C   // Current() command code (signed mA)
#define BQ27220_VOLT_REG 0x08  // Voltage() command code (mV)

// Analog DS3231 RTC I2C
#define I2C_ADDR_DS3231 0x68  // RTC I2C address
#define DS3231_SEC_REG 0x00   // Seconds command code (BCD)

// QST QMI8658 IMU I2C
#define I2C_ADDR_QMI8658 0x6B        // IMU I2C address
#define I2C_ADDR_QMI8658_ALT 0x6A    // IMU I2C fallback address
#define QMI8658_WHO_AM_I_REG 0x00    // WHO_AM_I command code
#define QMI8658_WHO_AM_I_VALUE 0x05  // WHO_AM_I expected value
#define QMI8658_CTRL1_REG 0x02       // SPI/auto-increment config
#define QMI8658_CTRL2_REG 0x03       // Accel ODR + full-scale range
#define QMI8658_CTRL3_REG 0x04       // Gyro Config (DPS + ODR)
#define QMI8658_CTRL7_REG 0x08       // Sensor enable (aEN=bit0, gEN=bit1)
#define QMI8658_ACCEL_X_L 0x35      // Accel X low byte (LE 16-bit, signed)
#define QMI8658_ACCEL_Y_L 0x37      // Accel Y low byte (LE 16-bit, signed)
#define QMI8658_ACCEL_Z_L 0x39      // Accel Z low byte (LE 16-bit, signed)
#define QMI8658_GYRO_X_L  0x3B      // Gyro X low byte (LE 16-bit, signed)
#define QMI8658_GYRO_Y_L  0x3D      // Gyro Y low byte (LE 16-bit, signed)
#define QMI8658_GYRO_Z_L  0x3F      // Gyro Z low byte (LE 16-bit, signed)
#define QMI8658_CTRL2_2G_125HZ 0x06  // CTRL2: ±2G full scale, 125 Hz ODR
#define QMI8658_CTRL7_ACCEL_EN 0x01 // CTRL7: accel only
#define QMI8658_CTRL7_GYRO_EN 0x02  // CTRL7: gyro only

namespace X3GPIO {
// Read a 16-bit little-endian I2C register. Returns false on bus error.
bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue);

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* outValue);
bool writeI2CReg8(uint8_t addr, uint8_t reg, uint8_t value);

uint8_t resolveQMI8658Addr();
uint8_t initQMI8658();

bool readQMI8658AccelAxis(uint8_t addr, uint8_t reg, int16_t* outValue);
bool readQMI8658AccelY(uint8_t addr, int16_t* outY);
bool readQMI8658GyroAxis(uint8_t addr, uint8_t reg, int16_t* outValue);
bool readQMI8658GyroX(uint8_t addr, int16_t* outX);
bool readQMI8658GyroY(uint8_t addr, int16_t* outY);
bool readQMI8658GyroZ(uint8_t addr, int16_t* outZ);
bool shutdownQMI8658(uint8_t addr);
}  // namespace X3GPIO

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

  // Wait until the raw power-button GPIO reads HIGH (released) for a sustained period.
  // Uses the raw pin directly instead of the InputManager debounced state to avoid
  // the 5 ms debounce being fooled by mechanical switch bounce during release.
  void waitForStablePowerRelease();

  // Setup wake up GPIO and enter deep sleep
  void startDeepSleep();

  // Verify power button was held long enough after wakeup.
  // If verification fails, enters deep sleep and does not return.
  // Should only be called when wakeup reason is PowerButton.
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  // Check if USB is connected
  bool isUsbConnected() const;

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
