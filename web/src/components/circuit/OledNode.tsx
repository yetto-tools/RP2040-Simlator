import { useEffect, useRef, useState } from "react";
import { Handle, Position, type Node, type NodeProps } from "@xyflow/react";
import { api } from "../../api";
import { i2cInstanceForPins } from "../../picoPinout";

export interface OledNodeData extends Record<string, unknown> {
  pins: Map<string, number>; // handle id ("sda"/"scl") -> gpio
  // Another oled node exists on the canvas - the backend has one attach
  // slot for this device type, so only the most-recently-attached one wins.
  hasSibling?: boolean;
}
export type OledNodeType = Node<OledNodeData, "oled">;

const kWidth = 128;
const kHeight = 64;
const kPages = kHeight / 8;
const kAddr = 0x3c; // ssd1306.py's default - not user-configurable in v1
const kPollMs = 300; // 1024 bytes/frame - lighter than ST7789's, polled a bit faster

function base64ToBytes(b64: string): Uint8Array {
  const binary = atob(b64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

// v1 supports a single attached display (the backend has one dynamic-attach
// slot per I2C bus - see DebugSession::attach_ssd1306); adding a second OLED
// node on the same bus would silently steal it from the first. Not guarded
// against here - documented, not enforced, matching the ST7789 node's scope.
export function OledNode({ data }: NodeProps<OledNodeType>) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [attached, setAttached] = useState(false);

  const sda = data.pins.get("sda");
  const scl = data.pins.get("scl");
  const i2c = sda !== undefined && scl !== undefined ? i2cInstanceForPins(sda, scl) : null;
  const bothPinsWired = sda !== undefined && scl !== undefined;
  const invalidI2c = bothPinsWired && i2c === null;
  const ready = i2c !== null;

  // Attach lifecycle - same self-contained shape as Tft7789Node (no shared
  // App state to update afterward, so it calls the API directly rather than
  // routing through App.tsx like the single-GPIO nodes' press/change callbacks).
  // data.hasSibling is in the dep list too - see Tft7789Node.tsx's comment
  // on the identical effect: without it, a same-type sibling's unmount can
  // silently orphan this node (its cleanup unconditionally detaches the
  // shared backend slot, and nothing here would otherwise re-run to
  // re-attach it).
  useEffect(() => {
    if (!ready) return;
    let cancelled = false;
    api
      .attachSsd1306(i2c as number, kAddr)
      .then(() => {
        if (!cancelled) setAttached(true);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
      api.detachSsd1306().catch(() => {});
    };
  }, [ready, i2c, data.hasSibling]);

  // Same staleness tradeoff as Tft7789Node's showAttached: avoids a
  // synchronous setState-in-effect reset when `ready` drops.
  const showAttached = ready && attached;

  // Poll + render the GDDRAM while attached. Page-major 1bpp layout (see
  // ssd1306.h's gddram()): byte (col + page*kWidth), bit N = pixel row page*8+N.
  useEffect(() => {
    if (!showAttached) return;
    let cancelled = false;
    const draw = async () => {
      try {
        const { gddramBase64 } = await api.getSsd1306Framebuffer();
        if (cancelled) return;
        const bytes = base64ToBytes(gddramBase64);
        const ctx = canvasRef.current?.getContext("2d");
        if (!ctx) return;
        const img = ctx.createImageData(kWidth, kHeight);
        for (let page = 0; page < kPages; page++) {
          for (let col = 0; col < kWidth; col++) {
            const byte = bytes[col + page * kWidth] ?? 0;
            for (let bit = 0; bit < 8; bit++) {
              const on = (byte >> bit) & 1;
              const v = on ? 255 : 0;
              const idx = ((page * 8 + bit) * kWidth + col) * 4;
              img.data[idx + 0] = v;
              img.data[idx + 1] = v;
              img.data[idx + 2] = v;
              img.data[idx + 3] = 255;
            }
          }
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
    if (!bothPinsWired) placeholder = "wire SDA/SCL";
    else if (invalidI2c) {
      placeholder = "SDA/SCL must share an I2C bus";
      placeholderIsError = true;
    } else placeholder = "connecting...";
  }

  return (
    <div className="tft-node">
      <span className="component-node__label">
        OLED SSD1306{i2c !== null ? ` · I2C${i2c}` : ""}
        {data.hasSibling && (
          <span className="component-node__conflict-badge" title="Only one active at a time - the most recently attached wins">
            ⚠
          </span>
        )}
      </span>
      <div className="tft-node__body">
        <div className="tft-node__pins">
          {(["sda", "scl"] as const).map((id) => (
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
            className="tft-node__canvas oled-node__canvas"
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
