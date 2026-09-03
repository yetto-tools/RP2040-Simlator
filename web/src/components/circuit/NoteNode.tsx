import type { Node, NodeProps } from "@xyflow/react";

export interface NoteNodeData extends Record<string, unknown> {
  text: string;
  onTextChange: (nodeId: string, text: string) => void;
}
export type NoteNodeType = Node<NoteNodeData, "note">;

// A free-text annotation, not a component - no Handle (nothing electrical
// to wire), dashed border in CSS to read as "canvas note" rather than a
// device. Deletion is free: onNodesChange/applyNodeChanges in
// CircuitCanvas.tsx already handles Delete/Backspace on any selected,
// non-Pico node.
//
// The textarea needs `nodrag` (else typing would drag the node instead of
// placing the caret), which would leave nothing left to grab if it filled
// the whole node - hence the small label strip above it as a dedicated
// drag handle, the same header-then-body shape tft-node already uses.
export function NoteNode({ id, data }: NodeProps<NoteNodeType>) {
  return (
    <div className="note-node">
      <div className="note-node__handle">NOTE</div>
      <textarea
        className="note-node__text nodrag nowheel"
        value={data.text}
        placeholder="Note..."
        onChange={(e) => data.onTextChange(id, e.target.value)}
      />
    </div>
  );
}
