import type { PinState } from "../api";
import { isOnboardLed, kLeftCount, kNumGpio, pinCapabilities } from "../picoPinout";

interface Props {
  gpio: PinState[];
  onToggle: (pin: number, level: boolean) => void;
}

// GPIO_CTRL FUNCSEL values are pin-independent (same numeric meaning on
// every pin; only *which instance* - e.g. uart0 vs uart1 - depends on the
// pin, per pico-sdk's hardware/gpio.h gpio_function enum).
const FUNC_LABELS: Record<number, string> = {
  0: "XIP",
  1: "SPI",
  2: "UART",
  3: "I2C",
  4: "PWM",
  5: "SIO",
  6: "PIO0",
  7: "PIO1",
  8: "GPCK",
  9: "USB",
  31: "DISABLED",
};

function Cell({ pin, state, onToggle }: { pin: number; state?: PinState; onToggle: (pin: number, level: boolean) => void }) {
  const driving = state?.driving ?? false;
  const level = state?.level ?? false;
  const isInput = !driving;
  // SIO (plain software GPIO) and DISABLED are the common case the previous
  // per-function grouping made obvious for free; call out anything else so
  // that's not lost now that all 30 pins share one fixed layout.
  const funcsel = state?.funcsel;
  const funcLabel = funcsel !== undefined && funcsel !== 5 && funcsel !== 31 ? (FUNC_LABELS[funcsel] ?? `F${funcsel}`) : null;

  // What the pin is doing right now, then a blank line, then what it *can*
  // do (datasheet mux options) - the live badge above only covers the former.
  const caps = pinCapabilities(pin);
  const stateLine = isInput ? "Click to toggle external input level" : "Pin is driven by the firmware";
  const title = caps.length ? `${stateLine}\n\n${caps.join("\n")}` : stateLine;

  return (
    <button
      className={`pin-cell ${isInput ? "pin-cell--interactive" : ""}`}
      disabled={!isInput}
      onClick={() => onToggle(pin, !level)}
      title={title}
    >
      <span className="pin-cell__info">
        <span className="pin-cell__label">GP{pin}</span>
        <span className="pin-cell__dir">{driving ? "OUT" : "IN"}</span>
        {funcLabel && <span className="pin-cell__func">{funcLabel}</span>}
        {isOnboardLed(pin) && <span className="pin-cell__func pin-cell__func--led">LED</span>}
      </span>
      <span className={`pin-cell__led ${level ? "pin-cell__led--on" : ""}`} />
    </button>
  );
}

export function PinPanel({ gpio, onToggle }: Props) {
  const byPin = new Map<number, PinState>();
  for (const p of gpio) byPin.set(p.pin, p);

  return (
    <div className="pin-panel">
      <div className="panel-title">PINS</div>
      <div className="pin-table">
        {Array.from({ length: kLeftCount }, (_, i) => {
          const left = i;
          const right = kNumGpio - 1 - i;
          return (
            <div className="pin-row" key={i}>
              <Cell pin={left} state={byPin.get(left)} onToggle={onToggle} />
              <Cell pin={right} state={byPin.get(right)} onToggle={onToggle} />
            </div>
          );
        })}
      </div>
    </div>
  );
}
