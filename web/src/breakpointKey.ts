// Source-level breakpoints (VSCode-style: settable before compiling, kept
// across recompiles) are keyed by "file:line" rather than by address - the
// address only exists once /compile has produced a lineMap. Filenames are
// restricted to [A-Za-z0-9_-]+.ext (see FileExplorer's NAME_RE), so they
// never contain ':' and a plain split is unambiguous.
export function bpKey(file: string, line: number): string {
  return `${file}:${line}`;
}

export function parseBpKey(key: string): { file: string; line: number } {
  const idx = key.lastIndexOf(":");
  return { file: key.slice(0, idx), line: Number(key.slice(idx + 1)) };
}
