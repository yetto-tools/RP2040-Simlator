import { useCallback, useMemo } from "react";
import {
  Background,
  Controls,
  ConnectionMode,
  ReactFlow,
  addEdge,
  applyEdgeChanges,
  applyNodeChanges,
  type Connection,
  type Edge,
  type Node,
  type OnConnect,
  type OnEdgesChange,
  type OnNodesChange,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import type { PinState } from "../../api";
import { PICO_NODE_ID } from "../../picoPinout";
import { useCircuitWiring, useMultiPinWiring } from "../../useCircuitWiring";
import { PicoNode } from "./PicoNode";
import { LedNode, type LedNodeData } from "./LedNode";
import { ButtonNode, type ButtonNodeData } from "./ButtonNode";
import { PotNode, type PotNodeData } from "./PotNode";
import { BuzzerNode, type BuzzerNodeData } from "./BuzzerNode";
import { Tft7789Node, type Tft7789NodeData } from "./Tft7789Node";
import { Ili9341Node, type Ili9341NodeData } from "./Ili9341Node";
import { OledNode, type OledNodeData } from "./OledNode";
import { NoteNode, type NoteNodeData } from "./NoteNode";
import { Palette, type ComponentKind } from "./Palette";

const nodeTypes = {
  pico: PicoNode,
  led: LedNode,
  button: ButtonNode,
  pot: PotNode,
  buzzer: BuzzerNode,
  tft7789: Tft7789Node,
  ili9341: Ili9341Node,
  oled: OledNode,
  note: NoteNode,
};

// Approximate on-canvas footprint per kind, used only to pick a
// non-overlapping spot for a *newly added* node (see findFreeSpot below) -
// not pixel-exact, since nodes are freely draggable/resizable-by-content
// afterward anyway.
const FOOTPRINT: Record<ComponentKind, { w: number; h: number }> = {
  led: { w: 190, h: 60 },
  button: { w: 190, h: 60 },
  pot: { w: 190, h: 60 },
  buzzer: { w: 190, h: 60 },
  tft7789: { w: 190, h: 220 },
  ili9341: { w: 190, h: 280 },
  oled: { w: 190, h: 220 },
  note: { w: 200, h: 120 },
};

const GRID_COL_STEP = 260;
const GRID_ROW_STEP = 20;
const GRID_BASE_X = 320;
const GRID_BASE_Y = 20;
const GRID_MAX_COLS = 6;
const GRID_MAX_ROWS = 40;

function rectsOverlap(
  a: { x: number; y: number; w: number; h: number },
  b: { x: number; y: number; w: number; h: number },
): boolean {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

// Shelf-packing placement: scan a column-major grid of candidate slots and
// take the first one that doesn't overlap any existing node's approximate
// footprint. Existing nodes never move - this only decides where a new one
// lands, so the palette doesn't keep dropping components on top of
// whatever's already on the canvas (every multi-pin device added during
// verification needed a manual drag to un-overlap under the old fixed
// random-band placement).
function findFreeSpot(kind: ComponentKind, existing: Node[]): { x: number; y: number } {
  const footprint = FOOTPRINT[kind];
  const occupied = existing
    .filter((n) => n.type && n.type in FOOTPRINT)
    .map((n) => ({ x: n.position.x, y: n.position.y, ...FOOTPRINT[n.type as ComponentKind] }));

  for (let col = 0; col < GRID_MAX_COLS; col++) {
    const x = GRID_BASE_X + col * GRID_COL_STEP;
    for (let row = 0; row < GRID_MAX_ROWS; row++) {
      const y = GRID_BASE_Y + row * GRID_ROW_STEP;
      const candidate = { x, y, ...footprint };
      if (!occupied.some((r) => rectsOverlap(candidate, r))) return { x, y };
    }
  }
  // Fallback if every scanned slot is somehow full: below everything.
  const maxY = occupied.reduce((m, r) => Math.max(m, r.y + r.h), GRID_BASE_Y);
  return { x: GRID_BASE_X, y: maxY + GRID_ROW_STEP };
}

// Fixed, never persisted (see below) - always the same board at the same
// spot, so there's nothing to drag/delete/duplicate by mistake.
const PICO_NODE: Node = {
  id: PICO_NODE_ID,
  type: "pico",
  position: { x: 0, y: 0 },
  data: {},
  draggable: false,
  selectable: false,
  deletable: false,
};

// What's persisted (Project.circuit in App.tsx) - deliberately just the
// draggable component nodes + wires; the Pico node above is synthesized on
// every render instead, so old saved circuits can't end up with a stray or
// duplicate board.
export interface CircuitState {
  nodes: Node[];
  edges: Edge[];
}

interface Props {
  circuit: CircuitState;
  onChange: (circuit: CircuitState) => void;
  gpio: PinState[];
  onGpioPress: (pin: number) => void;
  onGpioRelease: (pin: number) => void;
  onAdcChange: (channel: number, raw12: number) => void;
}

export function CircuitCanvas({ circuit, onChange, gpio, onGpioPress, onGpioRelease, onAdcChange }: Props) {
  const wiring = useCircuitWiring(circuit.edges, gpio);
  const multiWiring = useMultiPinWiring(circuit.edges);

  // How many nodes of each multi-pin device type are on the canvas - the
  // backend has one attach slot per type (see e.g. DebugSession's
  // st7789_/ili9341_/ssd1306_ members), so a second node of the same kind
  // silently steals the slot from the first. Surfaced as a warning badge
  // rather than prevented outright (removing/rewiring either node resolves
  // it, and blocking the add would be more surprising than useful).
  const typeCounts = useMemo(() => {
    const counts = new Map<string, number>();
    for (const n of circuit.nodes) counts.set(n.type ?? "", (counts.get(n.type ?? "") ?? 0) + 1);
    return counts;
  }, [circuit.nodes]);

  const handleNoteTextChange = useCallback(
    (nodeId: string, text: string) => {
      onChange({
        ...circuit,
        nodes: circuit.nodes.map((n) => (n.id === nodeId ? { ...n, data: { ...n.data, text } } : n)),
      });
    },
    [circuit, onChange],
  );

  const displayNodes = useMemo<Node[]>(() => {
    const componentNodes = circuit.nodes.map((n) => {
      if (n.type === "led") {
        const data: LedNodeData = { wired: wiring.get(n.id) };
        return { ...n, data };
      }
      if (n.type === "button") {
        const data: ButtonNodeData = { wired: wiring.get(n.id), onPress: onGpioPress, onRelease: onGpioRelease };
        return { ...n, data };
      }
      if (n.type === "pot") {
        const data: PotNodeData = { wired: wiring.get(n.id), onChange: onAdcChange };
        return { ...n, data };
      }
      if (n.type === "buzzer") {
        const data: BuzzerNodeData = { wired: wiring.get(n.id) };
        return { ...n, data };
      }
      if (n.type === "tft7789") {
        const data: Tft7789NodeData = {
          pins: multiWiring.get(n.id) ?? new Map(),
          hasSibling: (typeCounts.get("tft7789") ?? 0) > 1,
        };
        return { ...n, data };
      }
      if (n.type === "ili9341") {
        const data: Ili9341NodeData = {
          pins: multiWiring.get(n.id) ?? new Map(),
          hasSibling: (typeCounts.get("ili9341") ?? 0) > 1,
        };
        return { ...n, data };
      }
      if (n.type === "oled") {
        const data: OledNodeData = {
          pins: multiWiring.get(n.id) ?? new Map(),
          hasSibling: (typeCounts.get("oled") ?? 0) > 1,
        };
        return { ...n, data };
      }
      if (n.type === "note") {
        const data: NoteNodeData = { text: (n.data as NoteNodeData).text ?? "", onTextChange: handleNoteTextChange };
        return { ...n, data };
      }
      return n;
    });
    return [PICO_NODE, ...componentNodes];
  }, [circuit.nodes, wiring, multiWiring, typeCounts, onGpioPress, onGpioRelease, onAdcChange, handleNoteTextChange]);

  const onNodesChange: OnNodesChange = useCallback(
    (changes) => {
      // The synthesized Pico node isn't in circuit.nodes - drop any change
      // referencing it rather than feeding applyNodeChanges an id it can't
      // find (draggable/deletable are already false, so in practice this
      // is just the occasional dimension-measurement change).
      const relevant = changes.filter((c) => !("id" in c) || c.id !== PICO_NODE_ID);
      onChange({ ...circuit, nodes: applyNodeChanges(relevant, circuit.nodes) });
    },
    [circuit, onChange],
  );

  const onEdgesChange: OnEdgesChange = useCallback(
    (changes) => {
      onChange({ ...circuit, edges: applyEdgeChanges(changes, circuit.edges) });
    },
    [circuit, onChange],
  );

  const onConnect: OnConnect = useCallback(
    (connection: Connection) => {
      onChange({ ...circuit, edges: addEdge(connection, circuit.edges) });
    },
    [circuit, onChange],
  );

  const handleAdd = useCallback(
    (kind: ComponentKind) => {
      const node: Node = {
        id: `${kind}-${crypto.randomUUID()}`,
        type: kind,
        position: findFreeSpot(kind, circuit.nodes),
        data: kind === "note" ? { text: "" } : {},
      };
      onChange({ ...circuit, nodes: [...circuit.nodes, node] });
    },
    [circuit, onChange],
  );

  return (
    <div className="circuit-canvas">
      <Palette onAdd={handleAdd} />
      <ReactFlow
        nodes={displayNodes}
        edges={circuit.edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
        connectionMode={ConnectionMode.Loose}
        colorMode="dark"
        fitView
      >
        <Background gap={18} />
        <Controls />
      </ReactFlow>
    </div>
  );
}
