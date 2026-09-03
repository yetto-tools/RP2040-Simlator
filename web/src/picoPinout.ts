// Static Raspberry Pi Pico / RP2040 pin capabilities - which peripherals a
// GPIO *can* be muxed to, independent of the simulator's live FUNCSEL state.
// Reference: Raspberry Pi Pico datasheet pinout (ADC/PWM/I2C/SPI/UART tables).
// Surfaced as a hover tooltip in PinPanel - the live function badge already
// shows what a pin *is* doing; this is what it *could* do.

export const kNumGpio = 30;
export const kLeftCount = kNumGpio / 2; // GP0..GP14 down the left, GP29..GP15
// down the right - the Pico board's own silkscreen wrap-around order.

// PicoNode.tsx's fixed node id, and its GPIO connection-handle id
// convention ("gp12") - shared with useCircuitWiring.ts. Lives here rather
// than in PicoNode.tsx so that file only exports the component (Fast
// Refresh needs a component-only module).
export const PICO_NODE_ID = "pico";

export function picoHandleId(gpio: number): string {
  return `gp${gpio}`;
}

export function gpioFromHandleId(id: string | null | undefined): number | null {
  if (!id) return null;
  const m = /^gp(\d+)$/.exec(id);
  return m ? Number(m[1]) : null;
}

// Power/control pins (datasheet "Pines de alimentacion y control") - not
// GPIOs, so they carry no PinState and useCircuitWiring's gpioFromHandleId
// correctly resolves them to null (a wire here is purely structural, e.g.
// a display's VCC/GND leg - nothing for the simulator to drive or read).
export interface PowerPin {
  id: string;
  label: string;
}
export const POWER_PINS: PowerPin[] = [
  { id: "pwr_3v3", label: "3V3" },
  { id: "pwr_3v3en", label: "3V3_EN" },
  { id: "pwr_vbus", label: "VBUS" },
  { id: "pwr_vsys", label: "VSYS" },
  { id: "pwr_gnd", label: "GND" },
];

const I2C0_SDA = [0, 4, 8, 12, 16, 20];
const I2C0_SCL = [1, 5, 9, 13, 17, 21];
const I2C1_SDA = [2, 6, 10, 14, 18, 26];
const I2C1_SCL = [3, 7, 11, 15, 19, 27];

const SPI0_RX = [0, 4, 16]; // MISO
const SPI0_TX = [3, 7, 19]; // MOSI
const SPI0_CLK = [2, 6, 18];
const SPI0_CS = [1, 5, 17];
const SPI1_RX = [8, 12];
const SPI1_TX = [11, 15];
const SPI1_CLK = [10, 14];
const SPI1_CS = [9, 13];

const UART0_TX = [0, 12, 16];
const UART0_RX = [1, 13, 17];
const UART1_TX = [4, 8];
const UART1_RX = [5, 9];

const RESERVED: Record<number, string> = {
  23: "SMPS power-save control (not on the header)",
  24: "VBUS sense (not on the header)",
  25: "onboard LED",
  29: "also senses VSYS",
};

function pwmChannel(gpio: number): string | null {
  // Slices repeat every 16 GPIOs; the Pico's own datasheet table only
  // documents it through GPIO23 (24-29 are the reserved/ADC pins above).
  if (gpio > 23) return null;
  const slice = Math.floor((gpio % 16) / 2);
  const channel = gpio % 2 === 0 ? "A" : "B";
  return `PWM${slice}${channel}`;
}

// The numeric ADC channel (0-3) a GPIO is wired to, or null if it isn't
// ADC-capable - the same 26-29 range Adc::kNumInputs' 4 GPIO inputs cover
// (channel 4 is the on-die temperature sensor, not reachable from a pin).
export function adcChannelForGpio(gpio: number): number | null {
  if (gpio < 26 || gpio > 29) return null;
  return gpio - 26;
}

// Which SPI instance a SCK+MOSI pair implies, or null if they don't share
// one (either pin isn't SPI-capable, or they're on different instances) -
// lets a multi-pin device node (the ST7789 TFT) infer the bus from wiring
// instead of asking the user to pick a number separately.
export function spiInstanceForPins(sck: number, mosi: number): 0 | 1 | null {
  if (SPI0_CLK.includes(sck) && SPI0_TX.includes(mosi)) return 0;
  if (SPI1_CLK.includes(sck) && SPI1_TX.includes(mosi)) return 1;
  return null;
}

// Which I2C instance an SDA+SCL pair implies, or null if they don't share
// one - same role as spiInstanceForPins, for the SSD1306 OLED node.
export function i2cInstanceForPins(sda: number, scl: number): 0 | 1 | null {
  if (I2C0_SDA.includes(sda) && I2C0_SCL.includes(scl)) return 0;
  if (I2C1_SDA.includes(sda) && I2C1_SCL.includes(scl)) return 1;
  return null;
}

function adcChannel(gpio: number): string | null {
  const ch = adcChannelForGpio(gpio);
  return ch === null ? null : `ADC${ch}`;
}

// One line per capability, most relevant first - fed straight into a
// title="" tooltip (Cell in PinPanel.tsx joins with "\n").
export function pinCapabilities(gpio: number): string[] {
  const lines: string[] = [];

  const adc = adcChannel(gpio);
  if (adc) lines.push(adc);
  const pwm = pwmChannel(gpio);
  if (pwm) lines.push(pwm);

  const i2c: string[] = [];
  if (I2C0_SDA.includes(gpio)) i2c.push("I2C0 SDA");
  if (I2C0_SCL.includes(gpio)) i2c.push("I2C0 SCL");
  if (I2C1_SDA.includes(gpio)) i2c.push("I2C1 SDA");
  if (I2C1_SCL.includes(gpio)) i2c.push("I2C1 SCL");
  if (i2c.length) lines.push(i2c.join(" / "));

  const spi: string[] = [];
  if (SPI0_RX.includes(gpio)) spi.push("SPI0 RX");
  if (SPI0_TX.includes(gpio)) spi.push("SPI0 TX");
  if (SPI0_CLK.includes(gpio)) spi.push("SPI0 CLK");
  if (SPI0_CS.includes(gpio)) spi.push("SPI0 CS");
  if (SPI1_RX.includes(gpio)) spi.push("SPI1 RX");
  if (SPI1_TX.includes(gpio)) spi.push("SPI1 TX");
  if (SPI1_CLK.includes(gpio)) spi.push("SPI1 CLK");
  if (SPI1_CS.includes(gpio)) spi.push("SPI1 CS");
  if (spi.length) lines.push(spi.join(" / "));

  const uart: string[] = [];
  if (UART0_TX.includes(gpio)) uart.push("UART0 TX");
  if (UART0_RX.includes(gpio)) uart.push("UART0 RX");
  if (UART1_TX.includes(gpio)) uart.push("UART1 TX");
  if (UART1_RX.includes(gpio)) uart.push("UART1 RX");
  if (uart.length) lines.push(uart.join(" / "));

  const reserved = RESERVED[gpio];
  if (reserved) lines.push(reserved);

  return lines;
}

export function isOnboardLed(gpio: number): boolean {
  return gpio === 25;
}
