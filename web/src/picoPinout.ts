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

// The physical 40-pin edge header, in the Pico board's own silkscreen
// order and position (datasheet pinout diagram) - what PicoNode.tsx draws.
// Distinct from kNumGpio/kLeftCount above, which is PinPanel's own compact
// wrap-around table order and unrelated to physical pin placement.
export type BoardPinKind = "gpio" | "gnd" | "power";

export interface BoardPin {
  header: number; // physical header pin number, 1-40 (0 = not on the header)
  kind: BoardPinKind;
  gpio?: number; // present when kind === "gpio"
  label: string;
  // Wire-connection handle id. GPIO pins share the "gp<N>" convention (see
  // picoHandleId) so useCircuitWiring's gpioFromHandleId resolves them to a
  // live PinState; GND/power ids never match that pattern, so a wire there
  // correctly resolves to a structural-only connection (nothing to drive or
  // read - e.g. a display's VCC/GND leg).
  handleId: string;
}

function gpioPin(header: number, gpio: number): BoardPin {
  return { header, kind: "gpio", gpio, label: `GP${gpio}`, handleId: picoHandleId(gpio) };
}
function gndPin(header: number): BoardPin {
  return { header, kind: "gnd", label: "GND", handleId: `pwr_gnd_${header}` };
}
function powerPin(header: number, label: string, slug: string): BoardPin {
  return { header, kind: "power", label, handleId: `pwr_${slug}` };
}

// Left edge, header pins 1-20, top to bottom.
export const PICO_HEADER_LEFT: BoardPin[] = [
  gpioPin(1, 0), gpioPin(2, 1), gndPin(3),
  gpioPin(4, 2), gpioPin(5, 3),
  gpioPin(6, 4), gpioPin(7, 5), gndPin(8),
  gpioPin(9, 6), gpioPin(10, 7),
  gpioPin(11, 8), gpioPin(12, 9), gndPin(13),
  gpioPin(14, 10), gpioPin(15, 11),
  gpioPin(16, 12), gpioPin(17, 13), gndPin(18),
  gpioPin(19, 14), gpioPin(20, 15),
];

// Right edge, header pins 40 down to 21, top to bottom (pin 40 sits nearest
// the USB connector, pin 21 nearest the DEBUG header - matching the board's
// own silkscreen). GP23/24/25/29 are deliberately absent: they're real
// RP2040 GPIOs, but none of them is routed to this 40-pin header on the
// Pico (see the RESERVED notes below) - GP25 is the onboard LED only.
export const PICO_HEADER_RIGHT: BoardPin[] = [
  powerPin(40, "VBUS", "vbus"),
  powerPin(39, "VSYS", "vsys"),
  gndPin(38),
  powerPin(37, "3V3_EN", "3v3en"),
  powerPin(36, "3V3(OUT)", "3v3"),
  powerPin(35, "ADC_VREF", "adc_vref"),
  gpioPin(34, 28),
  gndPin(33),
  gpioPin(32, 27),
  gpioPin(31, 26),
  powerPin(30, "RUN", "run"),
  gpioPin(29, 22),
  gndPin(28),
  gpioPin(27, 21),
  gpioPin(26, 20),
  gpioPin(25, 19),
  gpioPin(24, 18),
  gndPin(23),
  gpioPin(22, 17),
  gpioPin(21, 16),
];

// The 3-pin SWD debug header below the board (not part of the 40-pin edge
// header).
export const DEBUG_PINS: BoardPin[] = [
  { header: 0, kind: "power", label: "SWCLK", handleId: "pwr_swclk" },
  { header: 0, kind: "gnd", label: "GND", handleId: "pwr_gnd_dbg" },
  { header: 0, kind: "power", label: "SWDIO", handleId: "pwr_swdio" },
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

// Shared by pinCapabilities (PinPanel's tooltip) and boardPinLabels
// (PicoNode's per-pin badges) so the two views never drift apart.
function altFunctionGroups(gpio: number) {
  return {
    adc: adcChannel(gpio),
    i2c: [
      ...(I2C0_SDA.includes(gpio) ? ["I2C0 SDA"] : []),
      ...(I2C0_SCL.includes(gpio) ? ["I2C0 SCL"] : []),
      ...(I2C1_SDA.includes(gpio) ? ["I2C1 SDA"] : []),
      ...(I2C1_SCL.includes(gpio) ? ["I2C1 SCL"] : []),
    ],
    spi: [
      ...(SPI0_RX.includes(gpio) ? ["SPI0 RX"] : []),
      ...(SPI0_TX.includes(gpio) ? ["SPI0 TX"] : []),
      ...(SPI0_CLK.includes(gpio) ? ["SPI0 CLK"] : []),
      ...(SPI0_CS.includes(gpio) ? ["SPI0 CS"] : []),
      ...(SPI1_RX.includes(gpio) ? ["SPI1 RX"] : []),
      ...(SPI1_TX.includes(gpio) ? ["SPI1 TX"] : []),
      ...(SPI1_CLK.includes(gpio) ? ["SPI1 CLK"] : []),
      ...(SPI1_CS.includes(gpio) ? ["SPI1 CS"] : []),
    ],
    uart: [
      ...(UART0_TX.includes(gpio) ? ["UART0 TX"] : []),
      ...(UART0_RX.includes(gpio) ? ["UART0 RX"] : []),
      ...(UART1_TX.includes(gpio) ? ["UART1 TX"] : []),
      ...(UART1_RX.includes(gpio) ? ["UART1 RX"] : []),
    ],
  };
}

// One line per capability, most relevant first - fed straight into a
// title="" tooltip (Cell in PinPanel.tsx joins with "\n").
export function pinCapabilities(gpio: number): string[] {
  const lines: string[] = [];
  const g = altFunctionGroups(gpio);

  if (g.adc) lines.push(g.adc);
  const pwm = pwmChannel(gpio);
  if (pwm) lines.push(pwm);
  if (g.i2c.length) lines.push(g.i2c.join(" / "));
  if (g.spi.length) lines.push(g.spi.join(" / "));
  if (g.uart.length) lines.push(g.uart.join(" / "));

  const reserved = RESERVED[gpio];
  if (reserved) lines.push(reserved);

  return lines;
}

// Compact per-function badges for the physical board diagram (PicoNode) -
// one entry per function rather than pinCapabilities' "A / B" joined lines,
// so each renders as its own small chip. PWM and the reserved-pin note are
// left out: the Pico's own datasheet pinout diagram doesn't show them on
// the header either (PWM repeats predictably from the GPIO number, and
// reserved pins - GP23/24/25/29 - never appear in PICO_HEADER_LEFT/RIGHT at
// all, see picoPinout.ts).
export function boardPinLabels(gpio: number): string[] {
  const g = altFunctionGroups(gpio);
  const lines: string[] = [];
  if (g.adc) lines.push(g.adc);
  lines.push(...g.i2c, ...g.spi, ...g.uart);
  return lines;
}

export function isOnboardLed(gpio: number): boolean {
  return gpio === 25;
}
