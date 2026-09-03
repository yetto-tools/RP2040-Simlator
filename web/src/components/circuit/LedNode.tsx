import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import type { WiredPin } from "../../useCircuitWiring";

export interface LedNodeData extends Record<string, unknown> {
  wired?: WiredPin;
}
export type LedNodeType = Node<LedNodeData, "led">;

// Lit state mirrors the same PinState the PINS panel already reads - an LED
// only lights when its wired GPIO is actively driven high by the firmware
// (an input pin, or one that's floating/unwired, never lights it).
export function LedNode({ data }: NodeProps<LedNodeType>) {
  const wired = data.wired;
  const lit = !!wired?.state?.driving && !!wired?.state?.level;

  return (
    <div className="component-node">
      <Handle className="component-node__handle" type="target" position={Position.Left} id="anode" />
      <div className={`led-bulb ${lit ? "led-bulb--on" : ""}`} />
      <span className="component-node__label">LED{wired ? ` · GP${wired.gpio}` : ""}</span>
    </div>
  );
}
