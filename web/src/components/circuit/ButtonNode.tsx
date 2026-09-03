import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import type { WiredPin } from "../../useCircuitWiring";

export interface ButtonNodeData extends Record<string, unknown> {
  wired?: WiredPin;
  onPress: (gpio: number) => void;
  onRelease: (gpio: number) => void;
}
export type ButtonNodeType = Node<ButtonNodeData, "button">;

// Press = api.setGpioExternal(gpio, true), release = clearGpioExternal -
// the exact same pair PinPanel.tsx's click-to-toggle already drives, just
// triggered from the canvas. onMouseLeave releases too, so dragging off
// the button mid-press can't leave the pin latched high.
export function ButtonNode({ data }: NodeProps<ButtonNodeType>) {
  const wired = data.wired;

  return (
    <div className="component-node">
      <button
        className="component-node__button nodrag"
        disabled={!wired}
        onMouseDown={() => wired && data.onPress(wired.gpio)}
        onMouseUp={() => wired && data.onRelease(wired.gpio)}
        onMouseLeave={() => wired && data.onRelease(wired.gpio)}
        title={wired ? `Hold to drive GP${wired.gpio} high` : "Wire to a GPIO pin first"}
      >
        <span className="component-node__button-glyph" />
      </button>
      <span className="component-node__label">Button{wired ? ` · GP${wired.gpio}` : ""}</span>
      <Handle className="component-node__handle" type="source" position={Position.Left} id="out" />
    </div>
  );
}
