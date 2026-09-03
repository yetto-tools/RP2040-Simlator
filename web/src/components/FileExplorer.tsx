import { useState } from "react";
import { IconFile, IconFilePlus, IconTrash } from "../icons";

interface Props {
  files: string[];
  activeFile: string;
  onSelect: (name: string) => void;
  onAdd: (name: string) => void;
  onRename: (oldName: string, newName: string) => void;
  onDelete: (name: string) => void;
}

const NAME_RE = /^[A-Za-z0-9_-]+\.(c|h|cpp|hpp|s)$/;

function validate(name: string, files: string[], initial: string): string | null {
  if (!NAME_RE.test(name)) {
    return `"${name}" isn't a valid name - use letters/digits/_/- and end in .c .h .cpp .hpp or .s`;
  }
  if (name !== initial && files.includes(name)) {
    return `"${name}" already exists in this project`;
  }
  return null;
}

export function FileExplorer({ files, activeFile, onSelect, onAdd, onRename, onDelete }: Props) {
  const [renaming, setRenaming] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState("");

  const startRename = (name: string) => {
    setRenaming(name);
    setRenameValue(name);
  };

  const commitRename = (original: string) => {
    const trimmed = renameValue.trim();
    setRenaming(null);
    if (trimmed === original) return;
    const err = validate(trimmed, files, original);
    if (err) {
      window.alert(err);
      return;
    }
    onRename(original, trimmed);
  };

  const handleAdd = () => {
    const name = window.prompt("File name (e.g. main.c, helpers.cpp, startup.s):", "");
    if (name === null) return;
    const trimmed = name.trim();
    const err = validate(trimmed, files, "");
    if (err) {
      window.alert(err);
      return;
    }
    onAdd(trimmed);
  };

  const handleDelete = (name: string) => {
    if (files.length <= 1) {
      window.alert("A project needs at least one file.");
      return;
    }
    onDelete(name);
  };

  return (
    <div className="file-explorer">
      <div className="file-explorer__header">
        <span>FILES</span>
        <button className="icon-btn" onClick={handleAdd} title="New file">
          <IconFilePlus size={14} />
        </button>
      </div>
      <div className="file-explorer__list">
        {files.map((name) => (
          <div
            key={name}
            className={`file-row ${name === activeFile ? "file-row--active" : ""}`}
            onClick={() => onSelect(name)}
            title="Click to open, double-click to rename"
          >
            {renaming === name ? (
              <input
                className="file-row__input"
                autoFocus
                value={renameValue}
                onClick={(e) => e.stopPropagation()}
                onChange={(e) => setRenameValue(e.target.value)}
                onBlur={() => commitRename(name)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") commitRename(name);
                  if (e.key === "Escape") setRenaming(null);
                }}
              />
            ) : (
              <>
                <IconFile size={14} className="file-row__icon" />
                <span className="file-row__name" onDoubleClick={(e) => { e.stopPropagation(); startRename(name); }}>
                  {name}
                </span>
                <button
                  className="file-row__delete"
                  onClick={(e) => {
                    e.stopPropagation();
                    handleDelete(name);
                  }}
                  title={`Delete ${name}`}
                >
                  <IconTrash size={12} />
                </button>
              </>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
