interface Props {
  direction: "col" | "row";
  value: number;
  min: number;
  max: number;
  onDelta: (deltaPx: number) => void;
  label: string;
}

const KEY_STEP = 16;

// Pointer-capture drag: capturing on the divider itself (rather than
// attaching window listeners) keeps receiving move events even when the
// cursor slips past the 1px line mid-drag. Also implements the WAI-ARIA
// "window splitter" pattern (focusable separator, arrow-key resize) so the
// layout isn't only adjustable with a mouse.
export function Resizer({ direction, value, min, max, onDelta, label }: Props) {
  const onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
  };
  const onPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!e.currentTarget.hasPointerCapture(e.pointerId)) return;
    onDelta(direction === "col" ? e.movementX : e.movementY);
  };
  const onPointerUp = (e: React.PointerEvent<HTMLDivElement>) => {
    e.currentTarget.releasePointerCapture(e.pointerId);
  };

  const growKey = direction === "col" ? "ArrowRight" : "ArrowDown";
  const shrinkKey = direction === "col" ? "ArrowLeft" : "ArrowUp";
  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (e.key === growKey) onDelta(KEY_STEP);
    else if (e.key === shrinkKey) onDelta(-KEY_STEP);
    else return;
    e.preventDefault();
  };

  return (
    <div
      className={`resizer resizer--${direction}`}
      role="separator"
      aria-orientation={direction === "col" ? "vertical" : "horizontal"}
      aria-label={label}
      aria-valuenow={Math.round(value)}
      aria-valuemin={min}
      aria-valuemax={max}
      tabIndex={0}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onKeyDown={onKeyDown}
    />
  );
}
