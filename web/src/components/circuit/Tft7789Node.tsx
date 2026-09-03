import { useEffect, useRef, useState } from "react";
import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import { api } from "../../api";
import { spiInstanceForPins } from "../../picoPinout";

export interface Tft7789NodeData extends Record<string, unknown> {
  pins: Map<string, number>; // handle id ("sck"/"mosi"/"cs"/"dc"/"rst") -> gpio
  // Another tft7789 node exists on the canvas - the backend has one attach
  // slot for this device type, so only the most-recently-attached one wins.
  hasSibling?: boolean;
}
export type Tft7789NodeType = Node<Tft7789NodeData, "tft7789">;

const kWidth = 240;
const kHeight = 240;
const kPollMs = 500; // a full frame is 115200 bytes - much heavier than /state, polled on its own slower loop

function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

// v1 supports a single attached display (the backend has one dynamic-attach
// slot - see DebugSession::attach_st7789); adding a second TFT node would
// silently steal it from the first. Not guarded against here - documented,
// not enforced, matching this phase's scope.
export function Tft7789Node({ data }: NodeProps<Tft7789NodeType>) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [attached, setAttached] = useState(false);

  const sck = data.pins.get("sck");
  const mosi = data.pins.get("mosi");
  const cs = data.pins.get("cs");
  const dc = data.pins.get("dc");
  const spi = sck !== undefined && mosi !== undefined ? spiInstanceForPins(sck, mosi) : null;
  const bothClockPinsWired = sck !== undefined && mosi !== undefined;
  const invalidSpi = bothClockPinsWired && spi === null;
  const ready = spi !== null && cs !== undefined && dc !== undefined;

  // Attach lifecycle: (re)attach whenever the resolved (spi, cs, dc) triple
  // changes, detach on unwire/unmount. Self-contained (calls the API
  // directly) rather than routed through App.tsx like the other nodes'
  // press/change callbacks - there's no shared App state this needs to
  // update afterward, unlike e.g. handleGpioPress's refreshState().
  //
  // data.hasSibling is in the dep list too, not just for the badge: the
  // backend has one attach slot per device type, so a same-type sibling's
  // cleanup unconditionally detaches it on unmount, even if *this* node is
  // still wired and mounted - without hasSibling here, this effect's own
  // deps never change in that case, so it would never re-run and this node
  // would be silently orphaned (attached=true in state, but nothing
  // actually attached backend-side). Re-running on every sibling-count
  // change re-attaches whichever node's effect runs last, self-healing it.
  useEffect(() => {
    if (!ready) return;
    let cancelled = false;
    api
      .attachSt7789(spi as number, cs as number, dc as number)
      .then(() => {
        if (!cancelled) setAttached(true);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
      api.detachSt7789().catch(() => {});
    };
  }, [ready, spi, cs, dc, data.hasSibling]);

  // `attached` only ever turns true from the effect above's async resolve,
  // and can go stale once `ready` drops (the cleanup detaches, but doesn't
  // reset the flag) - gating on both here is what keeps the UI correct
  // without a synchronous setState-in-effect reset for that case.
  const showAttached = ready && attached;

  // Poll + render the framebuffer while attached.
  useEffect(() => {
    if (!showAttached) return;
    let cancelled = false;
    const draw = async () => {
      try {
        const { rgb565Base64 } = await api.getSt7789Framebuffer();
        if (cancelled) return;
        const bytes = base64ToBytes(rgb565Base64);
        const ctx = canvasRef.current?.getContext("2d");
        if (!ctx) return;
        const img = ctx.createImageData(kWidth, kHeight);
        for (let i = 0; i < kWidth * kHeight; i++) {
          const px = (bytes[i * 2] << 8) | bytes[i * 2 + 1];
          const r = (px >> 11) & 0x1f;
          const g = (px >> 5) & 0x3f;
          const b = px & 0x1f;
          img.data[i * 4 + 0] = Math.round((r * 255) / 31);
          img.data[i * 4 + 1] = Math.round((g * 255) / 63);
          img.data[i * 4 + 2] = Math.round((b * 255) / 31);
          img.data[i * 4 + 3] = 255;
        }
        ctx.putImageData(img, 0, 0);
      } catch {
        // transient poll failure (e.g. mid-detach) - next tick retries
      }
    };
    draw();
    const id = setInterval(draw, kPollMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [showAttached]);

  // Distinguish "not wired yet" from "wired to pins that can't work
  // together" - collapsing both into one generic message (as before) reads
  // as a bug ("I *did* wire it") rather than a hint about what's wrong.
  let placeholder: string | null = null;
  let placeholderIsError = false;
  if (!showAttached) {
    if (!bothClockPinsWired) placeholder = "wire SCK/MOSI/CS/DC";
    else if (invalidSpi) {
      placeholder = "SCK/MOSI must share an SPI bus";
      placeholderIsError = true;
    } else if (cs === undefined || dc === undefined) placeholder = "wire CS/DC";
    else placeholder = "connecting...";
  }

  return (
    <div className="tft-node">
      <span className="component-node__label">
        TFT ST7789{spi !== null ? ` · SPI${spi}` : ""}
        {data.hasSibling && (
          <span className="component-node__conflict-badge" title="Only one active at a time - the most recently attached wins">
            ⚠
          </span>
        )}
      </span>
      <div className="tft-node__body">
        <div className="tft-node__pins">
          {(["sck", "mosi", "cs", "dc", "rst"] as const).map((id) => (
            <div className="tft-node__pin" key={id}>
              <Handle className="component-node__handle" type="target" position={Position.Left} id={id} />
              <span className="tft-node__pin-label">{id.toUpperCase()}</span>
            </div>
          ))}
        </div>
        <div className="tft-node__screen">
          <canvas ref={canvasRef} width={kWidth} height={kHeight} className="tft-node__canvas" />
          {placeholder && (
            <div className={`tft-node__placeholder ${placeholderIsError ? "tft-node__placeholder--error" : ""}`}>
              {placeholder}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
