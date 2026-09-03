import { Handle, Position } from "@xyflow/react";
import { kLeftCount, kNumGpio, picoHandleId, POWER_PINS } from "../../picoPinout";

const ROW_HEIGHT = 22;
const HEADER_HEIGHT = 34;
const FOOTER_HEIGHT = 46;

// The board, fixed on the canvas (not draggable/deletable - see
// CircuitCanvas.tsx). GP0..GP14 down the left, GP29..GP15 down the right -
// the same wrap-around order PinPanel.tsx already uses, so the two views
// agree on where a given GPIO "is".
export function PicoNode() {
  const rows = Array.from({ length: kLeftCount }, (_, i) => ({
    left: i,
    right: kNumGpio - 1 - i,
    top: HEADER_HEIGHT + i * ROW_HEIGHT + ROW_HEIGHT / 2,
  }));

  const gpioAreaHeight = HEADER_HEIGHT + kLeftCount * ROW_HEIGHT + 10;

  return (
    <div className="pico-node" style={{ height: gpioAreaHeight + FOOTER_HEIGHT }}>
      <div className="pico-node__label">Raspberry Pi Pico</div>
      {rows.map(({ left, right, top }) => (
        <div key={left}>
          <Handle
            className="pico-node__handle"
            type="source"
            position={Position.Left}
            id={picoHandleId(left)}
            style={{ top }}
          />
          <span className="pico-node__pin-label pico-node__pin-label--left" style={{ top }}>
            GP{left}
          </span>
          <span className="pico-node__pin-label pico-node__pin-label--right" style={{ top }}>
            GP{right}
          </span>
          <Handle
            className="pico-node__handle"
            type="source"
            position={Position.Right}
            id={picoHandleId(right)}
            style={{ top }}
          />
        </div>
      ))}

      {/* Power/control pins - not GPIOs, no live state; a wire here is
          structural (e.g. a display's VCC/GND leg), see picoPinout.ts. */}
      <div className="pico-node__power-row" style={{ top: gpioAreaHeight }}>
        {POWER_PINS.map((p) => (
          <div className="pico-node__power-pin" key={p.id}>
            <Handle className="pico-node__handle pico-node__handle--power" type="source" position={Position.Bottom} id={p.id} />
            <span className="pico-node__power-label">{p.label}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
