import type { StateSnapshot } from "../api";

interface Props {
  snapshot: StateSnapshot | null;
}

function hex(n: number): string {
  return "0x" + (n >>> 0).toString(16).padStart(8, "0");
}

// 32 bits grouped in nibbles, e.g. "0000 0000 0000 0000 0000 0000 0000 1010".
function binary(n: number): string {
  const bits = (n >>> 0).toString(2).padStart(32, "0");
  return bits.match(/.{1,4}/g)!.join(" ");
}

function decTooltip(n: number): string {
  return `dec ${n >>> 0}\nbin ${binary(n)}`;
}

const FLAG_NAMES: Record<string, string> = {
  N: "Negative",
  Z: "Zero",
  C: "Carry",
  V: "Overflow",
};

export function RegisterView({ snapshot }: Props) {
  if (!snapshot) {
    return (
      <div className="register-view">
        <div className="panel-title">REGISTERS</div>
        <div className="register-view__empty">No firmware loaded.</div>
      </div>
    );
  }

  const core: Array<[string, number]> = [
    ["pc", snapshot.pc],
    ["sp", snapshot.sp],
    ["lr", snapshot.lr],
    ["xpsr", snapshot.xpsr],
  ];

  // ARMv6-M's APSR packs only N,Z,C,V into xPSR[31:28] - Cortex-M0+ has no
  // Q flag (that's M3/M4). See CLAUDE.md "APSR: N, Z, C, V only".
  const flags: Array<[string, boolean]> = [
    ["N", ((snapshot.xpsr >>> 31) & 1) === 1],
    ["Z", ((snapshot.xpsr >>> 30) & 1) === 1],
    ["C", ((snapshot.xpsr >>> 29) & 1) === 1],
    ["V", ((snapshot.xpsr >>> 28) & 1) === 1],
  ];

  return (
    <div className="register-view">
      <div className="panel-title">REGISTERS</div>
      <div className="register-view__meta">
        <span title={`hex 0x${(snapshot.cycles >>> 0).toString(16)}`}>cycles {snapshot.cycles}</span>
        {snapshot.faultReason && <span className="register-view__fault">{snapshot.faultReason}</span>}
      </div>

      <div className="register-group">
        <div className="register-group__label">CORE</div>
        <div className="register-grid">
          {core.map(([name, val]) => (
            // Keying on name+value remounts the cell only when its value
            // actually changes, replaying the CSS flash animation for free -
            // no effect/timer needed to track and clear a "just changed" flag.
            <div key={`${name}:${val}`} className="register-cell" title={decTooltip(val)}>
              <span className="register-cell__name">{name}</span>
              <span className="register-cell__value">{hex(val)}</span>
            </div>
          ))}
        </div>
      </div>

      <div className="register-group">
        <div className="register-group__label">FLAGS (APSR)</div>
        <div className="flags-row">
          {flags.map(([name, set]) => (
            <div
              key={`${name}:${set}`}
              className={`flag-chip ${set ? "flag-chip--set" : ""}`}
              title={`${FLAG_NAMES[name]}: ${set ? "set" : "clear"}`}
            >
              {name}
            </div>
          ))}
        </div>
      </div>

      <div className="register-group">
        <div className="register-group__label">GENERAL</div>
        <div className="register-grid">
          {snapshot.r.map((v, i) => (
            <div key={`r${i}:${v}`} className="register-cell" title={decTooltip(v)}>
              <span className="register-cell__name">r{i}</span>
              <span className="register-cell__value">{hex(v)}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
