// api.ts - typed client for rp2040-lab-server's HTTP/JSON API
// (tools/lab_server). See its main.cpp for the authoritative route list.

const BASE = "http://localhost:8787";

export type RunStatus = "idle" | "running" | "halted" | "breakpoint" | "fault";

export interface PinState {
  pin: number;
  level: boolean;
  driving: boolean; // true = pad is actively driving (output); else input
  funcsel: number;
}

export interface StateSnapshot {
  loaded: boolean;
  status: RunStatus;
  faultReason: string;
  pc: number;
  sp: number;
  lr: number;
  xpsr: number;
  cycles: number;
  r: number[]; // r0..r12
  gpio: PinState[];
  uart0: string; // drained since the last poll
  uart1: string;
}

export interface LineAddr {
  file: string; // basename, e.g. "main.c"
  line: number;
  addr: number;
}

// A flat (no subdirectories) project file - BACKLOG.md P10.4 multi-file
// editing, not a general multi-file CMake project.
export interface SourceFile {
  name: string;
  content: string;
}

// "freestanding" - a single .c file with a hand-written _start, no libc, no
// pico-sdk (matches tests/fixtures/sum.c). "pico_sdk" - the same file built
// as a real pico-sdk project (pico_stdlib + the hardware/* libs this
// simulator implements) - see BACKLOG.md P10.3.
export type CompileMode = "freestanding" | "pico_sdk";

export interface CompileResult {
  ok: boolean;
  log: string;
  elfBase64?: string;
  lineMap?: LineAddr[];
}

async function postJson<T>(path: string, body: unknown): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const json = (await res.json()) as T & { error?: string };
  if (!res.ok) throw new Error(json.error ?? `${path} failed (${res.status})`);
  return json;
}

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(`${BASE}${path}`);
  const json = (await res.json()) as T & { error?: string };
  if (!res.ok) throw new Error(json.error ?? `${path} failed (${res.status})`);
  return json;
}

export const api = {
  health: () => getJson<{ ok: boolean }>("/health"),
  state: () => getJson<StateSnapshot>("/state"),

  compile: (files: SourceFile[], mode: CompileMode = "freestanding") =>
    postJson<CompileResult>("/compile", { files, mode }),

  load: (bytesBase64: string, kind: "elf" | "uf2", fromEntry?: boolean) =>
    postJson<StateSnapshot>("/load", { bytesBase64, kind, fromEntry }),

  run: () => postJson<{ ok: boolean }>("/run", {}),
  pause: () => postJson<{ ok: boolean }>("/pause", {}),
  step: () => postJson<StateSnapshot>("/step", {}),

  addBreakpoint: (addr: number) =>
    postJson<{ breakpoints: number[] }>("/breakpoints", { action: "add", addr }),
  removeBreakpoint: (addr: number) =>
    postJson<{ breakpoints: number[] }>("/breakpoints", { action: "remove", addr }),

  setGpioExternal: (pin: number, level: boolean) =>
    postJson<{ ok: boolean }>(`/gpio/${pin}/external`, { level }),
  clearGpioExternal: (pin: number) =>
    postJson<{ ok: boolean }>(`/gpio/${pin}/external`, { clear: true }),

  // channel: 0-4 (4 GPIO-backed ADC inputs + the temperature sensor).
  // raw12: a 12-bit ADC code (0-4095), not a voltage - see Adc::set_input.
  setAdcExternal: (channel: number, raw12: number) =>
    postJson<{ ok: boolean }>(`/adc/${channel}/external`, { raw12 }),

  feedUart: (n: 0 | 1, text: string) =>
    postJson<{ ok: boolean }>(`/uart/${n}/feed`, { text }),

  // A virtual ST7789 TFT (circuit-editor device, not an RP2040 peripheral)
  // wired to spi(spi)'s SCK/MOSI and the given CS/DC GPIOs.
  attachSt7789: (spi: number, cs: number, dc: number) =>
    postJson<{ ok: boolean }>("/st7789/attach", { spi, cs, dc }),
  detachSt7789: () => postJson<{ ok: boolean }>("/st7789/detach", {}),
  getSt7789Framebuffer: () => getJson<{ rgb565Base64: string }>("/st7789/framebuffer"),

  // A virtual ILI9341 TFT (circuit-editor device, not an RP2040 peripheral -
  // same command decoder as ST7789, see peripherals/ili9341.h) wired to
  // spi(spi)'s SCK/MOSI and the given CS/DC GPIOs.
  attachIli9341: (spi: number, cs: number, dc: number) =>
    postJson<{ ok: boolean }>("/ili9341/attach", { spi, cs, dc }),
  detachIli9341: () => postJson<{ ok: boolean }>("/ili9341/detach", {}),
  getIli9341Framebuffer: () => getJson<{ rgb565Base64: string }>("/ili9341/framebuffer"),

  // A virtual SSD1306 OLED (circuit-editor device, not an RP2040 peripheral)
  // wired to i2c(i2c)'s slave slot at the given 7-bit address.
  attachSsd1306: (i2c: number, addr: number) =>
    postJson<{ ok: boolean }>("/ssd1306/attach", { i2c, addr }),
  detachSsd1306: () => postJson<{ ok: boolean }>("/ssd1306/detach", {}),
  getSsd1306Framebuffer: () => getJson<{ gddramBase64: string }>("/ssd1306/framebuffer"),
};

// Base64 <-> bytes, for turning a File upload (.elf/.uf2) into the
// bytesBase64 field /load expects.
export function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}
