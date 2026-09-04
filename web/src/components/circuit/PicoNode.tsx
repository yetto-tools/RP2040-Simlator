import { Handle, Position } from "@xyflow/react";
import { boardPinLabels, DEBUG_PINS, PICO_HEADER_LEFT, PICO_HEADER_RIGHT, type BoardPin } from "../../picoPinout";

const GPIO_ROW_HEIGHT = 34; // taller than a bare GND row - it carries alt-function badges
const GND_ROW_HEIGHT = 15;
const HEADER_HEIGHT = 46; // room for the title bar + the "LED (GP25)" marker below it
const DEBUG_ROW_HEIGHT = 44;

function rowHeight(pin: BoardPin): number {
  return pin.kind === "gpio" ? GPIO_ROW_HEIGHT : GND_ROW_HEIGHT;
}

// Cumulative {top, height} for each pin in a column, in the pico-node's own
// coordinate space, so rows of different heights still stack cleanly.
function layout(pins: BoardPin[], top0: number): { pin: BoardPin; top: number; height: number }[] {
  let top = top0;
  return pins.map((pin) => {
    const height = rowHeight(pin);
    const row = { pin, top, height };
    top += height;
    return row;
  });
}

// The board, fixed on the canvas (not draggable/deletable - see
// CircuitCanvas.tsx). Every pin is drawn at its real physical header
// position (PICO_HEADER_LEFT/RIGHT, datasheet pinout diagram): left edge
// pins 1-20 top to bottom, right edge pins 40-21 top to bottom, GND
// interspersed exactly where the silkscreen puts it, power/ADC_VREF/RUN in
// their real slots, and a 3-pin SWD debug header below. GP23/24/25/29 are
// deliberately not wireable here - the real 40-pin header doesn't expose
// them either (see picoPinout.ts); GP25 (the onboard LED) is shown as a
// decorative marker only, matching the board's own silkscreen.
export function PicoNode() {
  const left = layout(PICO_HEADER_LEFT, HEADER_HEIGHT);
  const right = layout(PICO_HEADER_RIGHT, HEADER_HEIGHT);
  const gpioAreaHeight = Math.max(left[left.length - 1].top + left[left.length - 1].height, right[right.length - 1].top + right[right.length - 1].height) + 6;

  return (
    <div className="pico-node" style={{ height: gpioAreaHeight + DEBUG_ROW_HEIGHT }}>
      <div className="pico-node__label">Raspberry Pi Pico</div>
      <div className="pico-node__led">LED (GP25)</div>

      {left.map((row) => (
        <PinRow key={row.pin.handleId} row={row} side="left" />
      ))}
      {right.map((row) => (
        <PinRow key={row.pin.handleId} row={row} side="right" />
      ))}

      {/* 3-pin SWD debug header, centered below the 40-pin edge header. */}
      <div className="pico-node__debug-row" style={{ top: gpioAreaHeight }}>
        {DEBUG_PINS.map((pin) => (
          <div className="pico-node__debug-pin" key={pin.handleId}>
            <Handle
              className="pico-node__handle pico-node__handle--power"
              type="source"
              position={Position.Bottom}
              id={pin.handleId}
            />
            <span className="pico-node__debug-label">{pin.label}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function PinRow({ row, side }: { row: { pin: BoardPin; top: number; height: number }; side: "left" | "right" }) {
  const { pin, top, height } = row;
  const badges = pin.kind === "gpio" && pin.gpio !== undefined ? boardPinLabels(pin.gpio) : [];
  const badgeEl = badges.length > 0 && <span className="pico-node__badges">{badges.join(" / ")}</span>;
  const numEl = <span className="pico-node__header-num">{pin.header}</span>;
  const labelEl = <span className="pico-node__pin-label">{pin.label}</span>;

  return (
    <div
      className={`pico-node__row pico-node__row--${side} pico-node__row--${pin.kind}`}
      style={{ top, height }}
      title={badges.length ? badges.join(" / ") : undefined}
    >
      <Handle
        className={`pico-node__handle ${pin.kind !== "gpio" ? "pico-node__handle--power" : ""}`}
        type="source"
        position={side === "left" ? Position.Left : Position.Right}
        id={pin.handleId}
      />
      {side === "left" ? (
        <>
          {badgeEl}
          {numEl}
          {labelEl}
        </>
      ) : (
        <>
          {labelEl}
          {numEl}
          {badgeEl}
        </>
      )}
    </div>
  );
}
