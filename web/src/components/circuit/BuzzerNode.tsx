import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import { IconBuzzer } from "../../icons";
import type { WiredPin } from "../../useCircuitWiring";

export interface BuzzerNodeData extends Record<string, unknown> {
  wired?: WiredPin;
}
export type BuzzerNodeType = Node<BuzzerNodeData, "buzzer">;

// v1: a purely visual on/off indicator, same data path as LedNode (driven
// high = "buzzing"). Real audio (a Web Audio tone at the wired PWM slice's
// actual frequency) is an explicit stretch goal - see BACKLOG.md - not
// attempted here, so a fast PWM tone shows as a flicker rather than sound.
export function BuzzerNode({ data }: NodeProps<BuzzerNodeType>) {
  const wired = data.wired;
  const buzzing = !!wired?.state?.driving && !!wired?.state?.level;

  return (
    <div className="component-node">
      <Handle className="component-node__handle" type="target" position={Position.Left} id="in" />
      <span className={`buzzer-icon ${buzzing ? "buzzer-icon--on" : ""}`}>
        <IconBuzzer size={18} />
      </span>
      <span className="component-node__label">Buzzer{wired ? ` · GP${wired.gpio}` : ""}</span>
    </div>
  );
}
