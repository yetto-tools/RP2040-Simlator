import { useCallback, useState } from "react";

const STORAGE_PREFIX = "rp2040lab.layout.";

function clamp(n: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, n));
}

// Panel sizes (sidebar width, console height, ...) persisted across reloads
// so the layout the user tuned stays put - same pattern as the project
// auto-save in App.tsx.
export function usePersistedSize(key: string, initial: number, min: number, max: number) {
  const [size, setSize] = useState<number>(() => {
    const raw = localStorage.getItem(STORAGE_PREFIX + key);
    const n = raw ? Number(raw) : NaN;
    return Number.isFinite(n) ? clamp(n, min, max) : initial;
  });

  const adjust = useCallback(
    (deltaPx: number) => {
      setSize((prev) => {
        const next = clamp(prev + deltaPx, min, max);
        localStorage.setItem(STORAGE_PREFIX + key, String(next));
        return next;
      });
    },
    [key, min, max],
  );

  return [size, adjust] as const;
}
