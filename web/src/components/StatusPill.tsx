import type { RunStatus } from "../api";

const STATUS_META: Record<RunStatus, { label: string; className: string }> = {
  idle: { label: "IDLE", className: "status-pill--idle" },
  running: { label: "RUNNING", className: "status-pill--running" },
  halted: { label: "HALTED", className: "status-pill--halted" },
  breakpoint: { label: "BREAKPOINT", className: "status-pill--breakpoint" },
  fault: { label: "FAULT", className: "status-pill--fault" },
};

export function StatusPill({ status }: { status: RunStatus }) {
  const meta = STATUS_META[status];
  return (
    <span className={`status-pill ${meta.className}`}>
      <span className="status-pill__dot" />
      {meta.label}
    </span>
  );
}
