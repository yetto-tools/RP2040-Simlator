import type { LineAddr } from "../api";
import { parseBpKey } from "../breakpointKey";
import { IconTrash } from "../icons";

interface Props {
  breakpoints: Set<string>; // "file:line" keys - see breakpointKey.ts
  lineMap: LineAddr[];
  onSelectFile: (name: string) => void;
  onRemove: (key: string) => void;
  onClearAll: () => void;
}

export function BreakpointsList({ breakpoints, lineMap, onSelectFile, onRemove, onClearAll }: Props) {
  const resolvedKeys = new Set<string>();
  for (const la of lineMap) resolvedKeys.add(`${la.file}:${la.line}`);

  const rows = [...breakpoints].sort();

  return (
    <div className="breakpoints-list">
      <div className="file-explorer__header">
        <span>BREAKPOINTS</span>
        <div className="breakpoints-list__header-right">
          <span className="count-badge">{rows.length}</span>
          <button className="icon-btn" onClick={onClearAll} disabled={rows.length === 0} title="Clear all breakpoints">
            <IconTrash size={13} />
          </button>
        </div>
      </div>
      <div className="breakpoints-list__body">
        {rows.length === 0 && (
          <div className="breakpoints-list__empty">Click a line's gutter in the editor - works before compiling too.</div>
        )}
        {rows.map((key) => {
          const { file, line } = parseBpKey(key);
          const resolved = resolvedKeys.has(key);
          return (
            <div key={key} className="file-row" onClick={() => onSelectFile(file)} title={resolved ? undefined : "Pending - not yet mapped to an address; compile to activate"}>
              <span className={`breakpoints-list__dot ${resolved ? "" : "breakpoints-list__dot--pending"}`} />
              <span className="file-row__name">
                {file}:{line}
                {!resolved && <span className="breakpoints-list__pending-label"> pending</span>}
              </span>
              <button
                className="file-row__delete"
                onClick={(e) => {
                  e.stopPropagation();
                  onRemove(key);
                }}
                title="Remove breakpoint"
              >
                <IconTrash size={12} />
              </button>
            </div>
          );
        })}
      </div>
    </div>
  );
}
