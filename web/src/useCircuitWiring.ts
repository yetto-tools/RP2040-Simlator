import { useMemo } from "react";
import type { Edge } from "@xyflow/react";
import type { PinState } from "./api";
import { gpioFromHandleId, PICO_NODE_ID } from "./picoPinout";

export interface WiredPin {
  gpio: number;
  state?: PinState;
}

// For each non-Pico node, resolves the single Pico GPIO it's wired to (if
// any) plus that pin's live state - the one piece every component node
// (LED, Button, and later Pot/OLED/Buzzer) needs, computed once here rather
// than each node re-walking `edges` itself.
export function useCircuitWiring(edges: Edge[], gpio: PinState[]): Map<string, WiredPin> {
  return useMemo(() => {
    const byPin = new Map(gpio.map((p) => [p.pin, p] as const));
    const result = new Map<string, WiredPin>();
    for (const e of edges) {
      let componentNodeId: string | null = null;
      let gpioNum: number | null = null;
      if (e.source === PICO_NODE_ID) {
        componentNodeId = e.target;
        gpioNum = gpioFromHandleId(e.sourceHandle);
      } else if (e.target === PICO_NODE_ID) {
        componentNodeId = e.source;
        gpioNum = gpioFromHandleId(e.targetHandle);
      }
      if (componentNodeId !== null && gpioNum !== null) {
        result.set(componentNodeId, { gpio: gpioNum, state: byPin.get(gpioNum) });
      }
    }
    return result;
  }, [edges, gpio]);
}

// For multi-pin components (the ST7789 TFT: SCK/MOSI/CS/DC/RST) where
// useCircuitWiring's "one GPIO per node" shape doesn't fit - resolves every
// node's *own* handles to whichever GPIO is wired to each one. No live
// PinState here: a display's CS/DC resolution is about which GPIO numbers
// to hand the backend (POST /st7789/attach), not about polling their level
// client-side - the backend polls Gpio::level() itself per byte.
export function useMultiPinWiring(edges: Edge[]): Map<string, Map<string, number>> {
  return useMemo(() => {
    const result = new Map<string, Map<string, number>>();
    for (const e of edges) {
      let componentNodeId: string | null = null;
      let componentHandle: string | null = null;
      let gpioNum: number | null = null;
      if (e.source === PICO_NODE_ID) {
        componentNodeId = e.target;
        componentHandle = e.targetHandle ?? null;
        gpioNum = gpioFromHandleId(e.sourceHandle);
      } else if (e.target === PICO_NODE_ID) {
        componentNodeId = e.source;
        componentHandle = e.sourceHandle ?? null;
        gpioNum = gpioFromHandleId(e.targetHandle);
      }
      if (componentNodeId === null || componentHandle === null || gpioNum === null) continue;
      let pins = result.get(componentNodeId);
      if (!pins) {
        pins = new Map();
        result.set(componentNodeId, pins);
      }
      pins.set(componentHandle, gpioNum);
    }
    return result;
  }, [edges]);
}
