import { useEffect, useRef, useState } from "react";
import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import { adcChannelForGpio } from "../../picoPinout";
import type { WiredPin } from "../../useCircuitWiring";

export interface PotNodeData extends Record<string, unknown> {
  wired?: WiredPin;
  onChange: (channel: number, raw12: number) => void;
}
export type PotNodeType = Node<PotNodeData, "pot">;

const kMaxRaw12 = 4095; // 12-bit ADC full scale
// A real potentiometer's mechanical travel is ~270 degrees, not a full
// circle - drawn here as a knob swept clockwise from -135 (fully
// counter-clockwise, raw=0) to +135 (fully clockwise, raw=max), same
// convention as a physical knob's dead zone at the bottom.
const kSweepDeg = 270;
const kMinAngleDeg = -135;
const kKnobRadius = 16;
const kIndicatorLen = 11;

function rawToAngleDeg(raw: number): number {
  return kMinAngleDeg + (raw / kMaxRaw12) * kSweepDeg;
}

// atan2(dx, -dy) measures the angle clockwise from "straight up" (a compass
// bearing, not the usual atan2(y, x) convention) - matches rawToAngleDeg's
// "0 = up" reference so drag and render agree on what angle means.
function pointerToAngleDeg(dx: number, dy: number): number {
  return (Math.atan2(dx, -dy) * 180) / Math.PI;
}

function angleDegToRaw(angleDeg: number): number {
  const clamped = Math.min(kMinAngleDeg + kSweepDeg, Math.max(kMinAngleDeg, angleDeg));
  return Math.round(((clamped - kMinAngleDeg) / kSweepDeg) * kMaxRaw12);
}

// A real potentiometer only makes sense on one of the 4 ADC-capable GPIOs
// (26-29); wiring it anywhere else is a no-op (shown, not silently
// accepted) rather than pretending it works.
export function PotNode({ data }: NodeProps<PotNodeType>) {
  const wired = data.wired;
  const channel = wired ? adcChannelForGpio(wired.gpio) : null;
  const [raw, setRaw] = useState(Math.floor(kMaxRaw12 / 2));
  const knobRef = useRef<SVGSVGElement>(null);
  const draggingRef = useRef(false);

  // Push the slider's current position the moment it's (re)wired to a valid
  // ADC channel, so the reading isn't stale/zero until the user first drags
  // it - api.setAdcExternal is idempotent, so re-sending on every channel
  // change (not just the very first) is fine.
  useEffect(() => {
    if (channel !== null) data.onChange(channel, raw);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [channel]);

  const label = !wired ? "Pot" : channel === null ? `Pot · GP${wired.gpio} (not ADC)` : `Pot · ADC${channel}`;

  const updateFromPointer = (clientX: number, clientY: number) => {
    const rect = knobRef.current?.getBoundingClientRect();
    if (!rect) return;
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const angle = pointerToAngleDeg(clientX - cx, clientY - cy);
    const v = angleDegToRaw(angle);
    setRaw(v);
    if (channel !== null) data.onChange(channel, v);
  };

  const angleDeg = rawToAngleDeg(raw);
  const angleRad = (angleDeg * Math.PI) / 180;
  const indicatorX = kKnobRadius + kIndicatorLen * Math.sin(angleRad);
  const indicatorY = kKnobRadius - kIndicatorLen * Math.cos(angleRad);

  return (
    <div className="component-node">
      <span className="component-node__label">{label}</span>
      <svg
        ref={knobRef}
        className={`component-node__knob nodrag ${channel === null ? "component-node__knob--disabled" : ""}`}
        width={kKnobRadius * 2}
        height={kKnobRadius * 2}
        viewBox={`0 0 ${kKnobRadius * 2} ${kKnobRadius * 2}`}
        onPointerDown={(e) => {
          if (channel === null) return;
          e.currentTarget.setPointerCapture(e.pointerId);
          draggingRef.current = true;
          updateFromPointer(e.clientX, e.clientY);
        }}
        onPointerMove={(e) => {
          if (!draggingRef.current) return;
          updateFromPointer(e.clientX, e.clientY);
        }}
        onPointerUp={(e) => {
          draggingRef.current = false;
          e.currentTarget.releasePointerCapture(e.pointerId);
        }}
        role="slider"
        aria-valuemin={0}
        aria-valuemax={kMaxRaw12}
        aria-valuenow={raw}
      >
        <title>{channel === null ? "Wire to an ADC-capable pin (GP26-GP29) first" : `raw ${raw} / ${kMaxRaw12}`}</title>
        <circle cx={kKnobRadius} cy={kKnobRadius} r={kKnobRadius - 1.5} className="component-node__knob-body" />
        <line
          x1={kKnobRadius}
          y1={kKnobRadius}
          x2={indicatorX}
          y2={indicatorY}
          className="component-node__knob-indicator"
        />
      </svg>
      <Handle className="component-node__handle" type="target" position={Position.Left} id="wiper" />
    </div>
  );
}
