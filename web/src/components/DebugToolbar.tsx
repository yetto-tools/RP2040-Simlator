import type { CompileMode, RunStatus } from "../api";
import { IconCpu, IconHammer, IconPause, IconPlay, IconRefresh, IconStepForward, IconUpload } from "../icons";
import { StatusPill } from "./StatusPill";

interface Props {
  status: RunStatus;
  statusDetail?: string;
  loaded: boolean;
  compiling: boolean;
  compilerAvailable: boolean;
  mode: CompileMode;
  onModeChange: (mode: CompileMode) => void;
  onCompileAndLoad: () => void;
  onUpload: (file: File) => void;
  onRun: () => void;
  onPause: () => void;
  onStep: () => void;
  canReset: boolean;
  onReset: () => void;
}

export function DebugToolbar({
  status,
  statusDetail,
  loaded,
  compiling,
  compilerAvailable,
  mode,
  onModeChange,
  onCompileAndLoad,
  onUpload,
  onRun,
  onPause,
  onStep,
  canReset,
  onReset,
}: Props) {
  const running = status === "running";
  return (
    <header className="toolbar">
      <div className="toolbar__brand">
        <IconCpu size={18} />
        <span>
          RP2040 <b>Lab</b>
        </span>
      </div>
      <div className="toolbar__divider" />

      <select
        id="firmware-mode"
        name="firmware-mode"
        className="select"
        value={mode}
        onChange={(e) => onModeChange(e.target.value as CompileMode)}
        disabled={compiling}
        title="Firmware model to compile against"
        aria-label="Firmware model"
      >
        <option value="freestanding">Freestanding</option>
        <option value="pico_sdk">pico-sdk</option>
      </select>
      <button className="btn btn--primary" onClick={onCompileAndLoad} disabled={compiling || !compilerAvailable}>
        <IconHammer size={14} />
        {compiling ? (mode === "pico_sdk" ? "Compiling (pico-sdk)…" : "Compiling…") : "Compile + Load"}
      </button>
      <label className="btn btn--ghost">
        <IconUpload size={14} />
        Upload
        <input
          id="firmware-upload"
          name="firmware-upload"
          type="file"
          accept=".elf,.uf2"
          style={{ display: "none" }}
          aria-label="Upload firmware file"
          onChange={(e) => {
            const f = e.target.files?.[0];
            if (f) onUpload(f);
            e.target.value = "";
          }}
        />
      </label>
      {!compilerAvailable && <span className="toolbar__warning">Server unreachable</span>}

      <div className="toolbar__divider" />
      <button className="btn btn--icon" onClick={onRun} disabled={!loaded || running} title="Run (not real-time)">
        <IconPlay size={14} />
      </button>
      <button className="btn btn--icon" onClick={onPause} disabled={!running} title="Pause">
        <IconPause size={14} />
      </button>
      <button className="btn btn--icon" onClick={onStep} disabled={!loaded || running} title="Step">
        <IconStepForward size={14} />
      </button>
      <button
        className="btn btn--icon"
        onClick={onReset}
        disabled={!canReset}
        title="Reset - reload the current image and start over (cycles, registers, all peripherals)"
      >
        <IconRefresh size={14} />
      </button>
      {running && (
        <span
          className="toolbar__hint"
          title="Cycle-accurate simulation isn't synced to wall-clock time - sleep_ms()/busy-wait delays take real seconds proportional to simulated cycles, not to the delay they name."
        >
          cycle-accurate, not real-time
        </span>
      )}

      <div className="toolbar__spacer" />
      {statusDetail && <span className="toolbar__status-detail">{statusDetail}</span>}
      <StatusPill status={status} />
    </header>
  );
}
