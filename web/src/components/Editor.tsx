import { useEffect, useRef } from "react";
import MonacoEditor, { type Monaco, type OnMount } from "@monaco-editor/react";
import type * as monacoNs from "monaco-editor";
import type { LineAddr } from "../api";

interface Props {
  files: Record<string, string>;
  activeFile: string;
  onChangeFile: (name: string, value: string) => void;
  // Source-line -> PC address, from the last successful /compile. Empty
  // until a compile succeeds.
  lineMap: LineAddr[];
  breakpointAddrs: Set<number>;
  // Breakpoints on the active file that don't resolve to an address yet
  // (set before ever compiling, or on a line the last compile didn't map) -
  // VSCode shows these as a hollow gutter dot rather than nothing.
  pendingLines: Set<number>;
  onToggleBreakpointAtLine: (file: string, line: number) => void;
  currentPc?: number;
}

function languageFor(name: string): string {
  if (name.endsWith(".cpp") || name.endsWith(".hpp")) return "cpp";
  if (name.endsWith(".c") || name.endsWith(".h")) return "c";
  return "plaintext";
}

// Multi-file editing (BACKLOG.md P10.4) means one Monaco *model* per file -
// undo history and cursor position live on the model, not on whatever
// string happens to be passed as `value` this render, so this component is
// uncontrolled/imperative rather than the single-file `value`/`onChange`
// pattern it used before. Models are created/disposed/synced in an effect
// keyed on `files`; `editor.setModel()` swaps which one is visible.
export function Editor({
  files,
  activeFile,
  onChangeFile,
  lineMap,
  breakpointAddrs,
  pendingLines,
  onToggleBreakpointAtLine,
  currentPc,
}: Props) {
  const editorRef = useRef<monacoNs.editor.IStandaloneCodeEditor | null>(null);
  const monacoRef = useRef<Monaco | null>(null);
  const modelsRef = useRef<Map<string, monacoNs.editor.ITextModel>>(new Map());
  const decorationsRef = useRef<string[]>([]);
  const lastActiveFileRef = useRef<string | null>(null);

  // onMount fires exactly once per editor instance, so callbacks it
  // registers would otherwise close over whatever props existed on the
  // first render forever (real bug, found earlier this session via manual
  // testing of the single-file version). Read through refs kept current
  // every render instead.
  const filesRef = useRef(files);
  filesRef.current = files;
  const activeFileRef = useRef(activeFile);
  activeFileRef.current = activeFile;
  const onChangeFileRef = useRef(onChangeFile);
  onChangeFileRef.current = onChangeFile;
  const onToggleRef = useRef(onToggleBreakpointAtLine);
  onToggleRef.current = onToggleBreakpointAtLine;

  // Create/dispose/update models to match filesRef.current, then point the
  // editor at activeFileRef.current. Called both from the sync effect below
  // (on every files/activeFile change) and once from handleMount - Monaco
  // itself loads asynchronously, so onMount can fire *after* that effect's
  // first (no-op, editorRef not set yet) run, with nothing left to trigger
  // it again since refs alone don't cause a re-render.
  function syncModels(editor: monacoNs.editor.IStandaloneCodeEditor, monaco: Monaco) {
    const models = modelsRef.current;
    const current = filesRef.current;

    for (const name of Object.keys(current)) {
      const existing = models.get(name);
      if (!existing) {
        const uri = monaco.Uri.parse(`file:///${name}`);
        const model = monaco.editor.getModel(uri) ?? monaco.editor.createModel(current[name], languageFor(name), uri);
        models.set(name, model);
      } else if (existing.getValue() !== current[name]) {
        existing.setValue(current[name]);
      }
    }
    for (const [name, model] of models) {
      if (!(name in current)) {
        model.dispose();
        models.delete(name);
      }
    }

    const activeModel = models.get(activeFileRef.current) ?? null;
    if (editor.getModel() !== activeModel) editor.setModel(activeModel);
  }

  const handleMount: OnMount = (editor, monaco) => {
    editorRef.current = editor;
    monacoRef.current = monaco;
    // Colors lifted straight from index.css's :root palette so the editor
    // reads as part of this UI instead of an embedded VS Code widget - keep
    // the two in sync if that palette ever changes.
    monaco.editor.defineTheme("rp2040lab-dark", {
      base: "vs-dark",
      inherit: true,
      rules: [],
      colors: {
        "editor.background": "#0b100e",
        "editor.foreground": "#e8efe9",
        "editorLineNumber.foreground": "#56685c",
        "editorLineNumber.activeForeground": "#93a89b",
        "editor.lineHighlightBackground": "#17201a",
        "editorGutter.background": "#0b100e",
        "editorCursor.foreground": "#52e0c4",
        "editor.selectionBackground": "#33493c",
        "editorWidget.background": "#121915",
        "editorWidget.border": "#223129",
      },
    });
    monaco.editor.setTheme("rp2040lab-dark");
    editor.onMouseDown((e) => {
      if (e.target.type !== monaco.editor.MouseTargetType.GUTTER_GLYPH_MARGIN) return;
      const line = e.target.position?.lineNumber;
      if (line !== undefined) onToggleRef.current(activeFileRef.current, line);
    });
    // Editor-level (not model-level), so it keeps firing correctly across
    // setModel() swaps - always reflects whichever model is current.
    editor.onDidChangeModelContent(() => {
      onChangeFileRef.current(activeFileRef.current, editor.getValue());
    });
    syncModels(editor, monaco);
    // Monaco measures its container synchronously on mount, which in this
    // grid layout sometimes races ahead of the browser's own layout pass
    // and locks in a near-zero size (automaticLayout's ResizeObserver never
    // fires again afterward, since the container itself doesn't resize -
    // only the editor's stale internal measurement is wrong). One deferred
    // re-measure after the real layout has settled fixes it reliably.
    requestAnimationFrame(() => editor.layout());
  };

  // Keep Monaco's models in sync with `files`: create new ones, dispose
  // removed ones, push external content changes (mode switch, localStorage
  // restore) into existing ones - but only when the model's value actually
  // differs, so this editor's own edits (which round-trip back through
  // `files` unchanged) never reset the cursor/undo stack mid-typing.
  useEffect(() => {
    const editor = editorRef.current;
    const monaco = monacoRef.current;
    if (!editor || !monaco) return;
    syncModels(editor, monaco);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [files, activeFile]);

  // Re-render breakpoint dots + the current-PC highlight whenever any of
  // them change. lineMap lets us go address (what the backend tracks) ->
  // line (what Monaco needs to draw on), scoped to the file currently open
  // - a breakpoint in another file simply shows nothing until that file's
  // tab is selected.
  useEffect(() => {
    const editor = editorRef.current;
    const monaco = monacoRef.current;
    if (!editor || !monaco) return;

    // Decoration ids from a previous model aren't valid against a newly
    // set one - switching tabs without this drops or mis-targets them.
    if (lastActiveFileRef.current !== activeFile) {
      decorationsRef.current = [];
      lastActiveFileRef.current = activeFile;
    }

    const addrToLine = new Map<number, number>();
    for (const la of lineMap) {
      if (la.file === activeFile && !addrToLine.has(la.addr)) addrToLine.set(la.addr, la.line);
    }

    const decorations: monacoNs.editor.IModelDeltaDecoration[] = [];
    for (const addr of breakpointAddrs) {
      const line = addrToLine.get(addr);
      if (line === undefined) continue;
      decorations.push({
        range: new monaco.Range(line, 1, line, 1),
        options: { isWholeLine: false, glyphMarginClassName: "rp2040lab-bp-glyph" },
      });
    }
    for (const line of pendingLines) {
      decorations.push({
        range: new monaco.Range(line, 1, line, 1),
        options: { isWholeLine: false, glyphMarginClassName: "rp2040lab-bp-glyph-pending" },
      });
    }
    if (currentPc !== undefined) {
      const line = addrToLine.get(currentPc);
      if (line !== undefined) {
        decorations.push({
          range: new monaco.Range(line, 1, line, 1),
          options: {
            isWholeLine: true,
            className: "rp2040lab-current-line",
            linesDecorationsClassName: "rp2040lab-current-line-arrow",
          },
        });
      }
    }
    decorationsRef.current = editor.deltaDecorations(decorationsRef.current, decorations);
  }, [lineMap, breakpointAddrs, pendingLines, currentPc, activeFile]);

  return (
    <MonacoEditor
      height="100%"
      defaultLanguage="c"
      theme="rp2040lab-dark"
      onMount={handleMount}
      options={{ glyphMargin: true, minimap: { enabled: false }, fontSize: 13, automaticLayout: true }}
    />
  );
}
