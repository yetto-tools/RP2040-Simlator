import { useEffect, useRef, useState } from "react";
import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import { api } from "../../api";
import { spiInstanceForPins } from "../../picoPinout";

export interface Ili9341NodeData extends Record<string, unknown> {
  pins: Map<string, number>; // handle id ("sck"/"mosi"/"cs"/"dc"/"rst") -> gpio
  // Another ili9341 node exists on the canvas - the backend has one attach
  // slot for this device type, so only the most-recently-attached one wins.
  hasSibling?: boolean;
}
export type Ili9341NodeType = Node<Ili9341NodeData, "ili9341">;

const kWidth = 240;
const kHeight = 320;
const kPollMs = 500; // a full frame is 153600 bytes - same heavy-poll treatment as Tft7789Node

function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

// Same shape as Tft7789Node - see that file's comments for the rationale
// (self-contained attach lifecycle, showAttached staleness tradeoff). v1
// supports one attached ILI9341 (a second node would steal the backend's
// single attach slot); a separately-attached ST7789 on another SPI
// instance is unaffected - each device type owns its own slot.
export function Ili9341Node({ data }: NodeProps<Ili9341NodeType>) {
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

  // data.hasSibling is in the dep list too - see Tft7789Node.tsx's comment
  // on the identical effect: without it, a same-type sibling's unmount can
  // silently orphan this node (its cleanup unconditionally detaches the
  // shared backend slot, and nothing here would otherwise re-run to
  // re-attach it).
  useEffect(() => {
    if (!ready) return;
    let cancelled = false;
    api
      .attachIli9341(spi as number, cs as number, dc as number)
      .then(() => {
        if (!cancelled) setAttached(true);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
      api.detachIli9341().catch(() => {});
    };
  }, [ready, spi, cs, dc, data.hasSibling]);

  const showAttached = ready && attached;

  useEffect(() => {
    if (!showAttached) return;
    let cancelled = false;
    const draw = async () => {
      try {
        const { rgb565Base64 } = await api.getIli9341Framebuffer();
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
        TFT ILI9341{spi !== null ? ` · SPI${spi}` : ""}
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
          <canvas
            ref={canvasRef}
            width={kWidth}
            height={kHeight}
            className="tft-node__canvas ili9341-node__canvas"
          />
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
