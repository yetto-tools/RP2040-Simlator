import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api, bytesToBase64, type CompileMode, type LineAddr, type SourceFile, type StateSnapshot } from "./api";
import { bpKey, parseBpKey } from "./breakpointKey";
import { Editor } from "./components/Editor";
import { CircuitCanvas, type CircuitState } from "./components/circuit/CircuitCanvas";
import { FileExplorer } from "./components/FileExplorer";
import { BreakpointsList } from "./components/BreakpointsList";
import { Console } from "./components/Console";
import { PinPanel } from "./components/PinPanel";
import { DebugToolbar } from "./components/DebugToolbar";
import { RegisterView } from "./components/RegisterView";
import { Resizer } from "./components/Resizer";
import { usePersistedSize } from "./usePersistedSize";
import { IconChevronLeft, IconChevronRight, IconX } from "./icons";

const DEFAULT_SOURCE_FREESTANDING = `volatile unsigned *const GPIO_OUT = (unsigned *)0xd0000010;
volatile unsigned *const GPIO_OE  = (unsigned *)0xd0000020;
// IO_BANK0 GPIO25_CTRL - pins reset with FUNCSEL=0x1f (disabled); SIO's
// GPIO_OUT/GPIO_OE only reach the pad once FUNCSEL selects SIO (5). Real
// hardware needs this too - pico-sdk's gpio_init() does it for you.
volatile unsigned *const GPIO25_CTRL = (unsigned *)0x400140cc;

__attribute__((noreturn)) void _start(void) {
    *GPIO25_CTRL = 5;
    *GPIO_OE |= (1u << 25);
    for (;;) {
        *GPIO_OUT ^= (1u << 25);
        for (volatile int i = 0; i < 50000; ++i) {}
    }
}
`;

const DEFAULT_SOURCE_PICO_SDK = `#include "pico/stdlib.h"
#include <stdio.h>

int main(void) {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    int n = 0;
    while (true) {
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        printf("blink %d on\\n", n);
        sleep_ms(500);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        printf("blink %d off\\n", n);
        sleep_ms(500);
        n++;
    }
    return 0;
}
`;

const POLL_MS = 200;
const PROJECT_STORAGE_KEY = "rp2040lab.project.v1";
const SAVE_DEBOUNCE_MS = 500;

interface Project {
  mode: CompileMode;
  files: Record<string, string>;
  activeFile: string;
  circuit: CircuitState;
}

const EMPTY_CIRCUIT: CircuitState = { nodes: [], edges: [] };

// The last image actually sent to /load (compiled or uploaded), so "Reset"
// can power-cycle the simulator back onto it without recompiling/re-uploading.
interface LoadedImage {
  bytesBase64: string;
  kind: "elf" | "uf2";
  fromEntry?: boolean;
}

function defaultProject(mode: CompileMode): Project {
  const content = mode === "pico_sdk" ? DEFAULT_SOURCE_PICO_SDK : DEFAULT_SOURCE_FREESTANDING;
  return { mode, files: { "main.c": content }, activeFile: "main.c", circuit: EMPTY_CIRCUIT };
}

// Restored once at startup - BACKLOG.md P10.4's "save/load the whole
// project" is a single auto-persisted working set (not several named
// projects; that's a natural v2, not this pass), so closing the tab
// doesn't lose work.
function loadProject(): Project {
  try {
    const raw = localStorage.getItem(PROJECT_STORAGE_KEY);
    if (!raw) return defaultProject("freestanding");
    const parsed = JSON.parse(raw) as Partial<Project>;
    if (!parsed.files || !parsed.activeFile || !(parsed.activeFile in parsed.files)) {
      return defaultProject("freestanding");
    }
    return {
      mode: parsed.mode === "pico_sdk" ? "pico_sdk" : "freestanding",
      files: parsed.files,
      activeFile: parsed.activeFile,
      // Older saved projects (before the circuit editor) have no `circuit`
      // field - fall back to empty rather than rejecting the whole project.
      circuit: parsed.circuit ?? EMPTY_CIRCUIT,
    };
  } catch {
    return defaultProject("freestanding");
  }
}

export default function App() {
  const [project, setProject] = useState<Project>(loadProject);
  const { mode, files, activeFile, circuit } = project;
  const [activeTab, setActiveTab] = useState<"code" | "circuit">("code");
  const [compiling, setCompiling] = useState(false);
  const [compileLog, setCompileLog] = useState("");
  const [compilerAvailable, setCompilerAvailable] = useState(true);
  const [lineMap, setLineMap] = useState<LineAddr[]>([]);
  // Source-level, VSCode-style: keyed by "file:line" so a breakpoint can be
  // set before compiling and survives a recompile - see breakpointKey.ts.
  const [breakpoints, setBreakpoints] = useState<Set<string>>(new Set());
  const [loadGeneration, setLoadGeneration] = useState(0);
  const [snapshot, setSnapshot] = useState<StateSnapshot | null>(null);
  const [uart0Log, setUart0Log] = useState("");
  const [uart1Log, setUart1Log] = useState("");
  const [lastLoad, setLastLoad] = useState<LoadedImage | null>(null);
  const [sidebarOpen, setSidebarOpen] = useState(true);
  const [sidebarWidth, adjustSidebarWidth] = usePersistedSize("sidebar", 220, 160, 480);
  const [rightWidth, adjustRightWidth] = usePersistedSize("right", 320, 220, 560);
  const [consoleHeight, adjustConsoleHeight] = usePersistedSize("console", 200, 100, 480);
  const [registersHeight, adjustRegistersHeight] = usePersistedSize("registers", 320, 100, 600);

  const pollingRef = useRef(false);
  const saveTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Debounced so a fast typist doesn't hit localStorage on every keystroke.
  useEffect(() => {
    if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
    saveTimerRef.current = setTimeout(() => {
      localStorage.setItem(PROJECT_STORAGE_KEY, JSON.stringify(project));
    }, SAVE_DEBOUNCE_MS);
    return () => {
      if (saveTimerRef.current) clearTimeout(saveTimerRef.current);
    };
  }, [project]);

  // Resolve each source breakpoint to an address via the last successful
  // compile's lineMap - unresolved ones stay "pending" (shown in the UI,
  // never sent to the backend) until a compile maps their line.
  const resolvedAddrByKey = useMemo(() => {
    const m = new Map<string, number>();
    for (const la of lineMap) {
      const key = bpKey(la.file, la.line);
      if (!m.has(key)) m.set(key, la.addr);
    }
    return m;
  }, [lineMap]);

  const breakpointAddrs = useMemo(() => {
    const addrs = new Set<number>();
    for (const key of breakpoints) {
      const addr = resolvedAddrByKey.get(key);
      if (addr !== undefined) addrs.add(addr);
    }
    return addrs;
  }, [breakpoints, resolvedAddrByKey]);

  // Lines in the currently open file with a breakpoint that hasn't resolved
  // to an address yet - rendered as a hollow gutter dot (see Editor.tsx).
  const pendingLinesForActiveFile = useMemo(() => {
    const lines = new Set<number>();
    for (const key of breakpoints) {
      if (resolvedAddrByKey.has(key)) continue;
      const { file, line } = parseBpKey(key);
      if (file === activeFile) lines.add(line);
    }
    return lines;
  }, [breakpoints, resolvedAddrByKey, activeFile]);

  // Diffs against what was last pushed to the backend and sends only the
  // add/remove calls needed. loadGeneration forces a full resync after every
  // /load, since that power-cycles the simulator (and its breakpoint set)
  // even when the resolved addresses happen to come out unchanged.
  const syncedAddrsRef = useRef<Set<number>>(new Set());
  const syncedGenRef = useRef(-1);
  useEffect(() => {
    const prev = syncedGenRef.current === loadGeneration ? syncedAddrsRef.current : new Set<number>();
    for (const addr of breakpointAddrs) {
      if (!prev.has(addr)) api.addBreakpoint(addr).catch(() => {});
    }
    for (const addr of prev) {
      if (!breakpointAddrs.has(addr)) api.removeBreakpoint(addr).catch(() => {});
    }
    syncedAddrsRef.current = breakpointAddrs;
    syncedGenRef.current = loadGeneration;
  }, [breakpointAddrs, loadGeneration]);

  const refreshState = useCallback(async () => {
    try {
      const s = await api.state();
      setSnapshot(s);
      if (s.uart0) setUart0Log((prev) => prev + s.uart0);
      if (s.uart1) setUart1Log((prev) => prev + s.uart1);
    } catch {
      // server not reachable yet - keep last known state
    }
  }, []);

  useEffect(() => {
    api.health().catch(() => setCompilerAvailable(false));
    const id = setInterval(() => {
      if (pollingRef.current) return;
      pollingRef.current = true;
      refreshState().finally(() => {
        pollingRef.current = false;
      });
    }, POLL_MS);
    return () => clearInterval(id);
  }, [refreshState]);

  const handleCompileAndLoad = useCallback(async () => {
    setCompiling(true);
    setCompileLog("");
    try {
      const sourceFiles: SourceFile[] = Object.entries(files).map(([name, content]) => ({ name, content }));
      const result = await api.compile(sourceFiles, mode);
      setCompileLog(result.log);
      setLineMap(result.lineMap ?? []);
      if (!result.ok || !result.elfBase64) return;
      // Breakpoints are kept (they're keyed by file:line, not address) so a
      // recompile re-resolves them against the new lineMap automatically.
      // Freestanding firmware has no real vector table (jump straight to
      // _start); pico-sdk firmware does, and must reset through it normally
      // - see ARCHITECTURE.md "Local web lab" for why.
      const fromEntry = mode === "freestanding";
      const s = await api.load(result.elfBase64, "elf", fromEntry);
      setSnapshot(s);
      setLoadGeneration((g) => g + 1);
      setUart0Log("");
      setUart1Log("");
      setLastLoad({ bytesBase64: result.elfBase64, kind: "elf", fromEntry });
    } catch (err) {
      setCompileLog(String(err));
    } finally {
      setCompiling(false);
    }
  }, [files, mode]);

  const handleModeChange = useCallback((next: CompileMode) => {
    // The circuit is the physical wiring, independent of which firmware
    // mode is being tested against it - keep it (everything else about
    // "mode" resets to that mode's own default source).
    setProject((p) => ({ ...defaultProject(next), circuit: p.circuit }));
    setCompileLog("");
    setLineMap([]);
    setBreakpoints(new Set());
  }, []);

  const handleCircuitChange = useCallback((next: CircuitState) => {
    setProject((p) => ({ ...p, circuit: next }));
  }, []);

  const handleGpioPress = useCallback((pin: number) => {
    api.setGpioExternal(pin, true).then(refreshState);
  }, [refreshState]);

  const handleGpioRelease = useCallback((pin: number) => {
    api.clearGpioExternal(pin).then(refreshState);
  }, [refreshState]);

  const handleAdcChange = useCallback((channel: number, raw12: number) => {
    api.setAdcExternal(channel, raw12).catch(() => {});
  }, []);

  const handleUpload = useCallback(async (file: File) => {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const kind = file.name.toLowerCase().endsWith(".uf2") ? "uf2" : "elf";
    // A raw upload has no source, so lineMap goes empty and any breakpoints
    // become pending again (kept, not cleared - same file may be re-uploaded).
    setLineMap([]);
    const bytesBase64 = bytesToBase64(bytes);
    try {
      const s = await api.load(bytesBase64, kind);
      setSnapshot(s);
      setLoadGeneration((g) => g + 1);
      setUart0Log("");
      setUart1Log("");
      setLastLoad({ bytesBase64, kind });
    } catch (err) {
      setCompileLog(String(err));
    }
  }, []);

  // Power-cycles the simulator back onto the last-loaded image (same bytes,
  // no recompile/re-upload) - cycles, registers, and every peripheral reset
  // exactly like /load already does for a fresh compile (DebugSession::load
  // always replaces the Simulator instance), just without the compiler step.
  const handleReset = useCallback(async () => {
    if (!lastLoad) return;
    try {
      const s = await api.load(lastLoad.bytesBase64, lastLoad.kind, lastLoad.fromEntry);
      setSnapshot(s);
      setLoadGeneration((g) => g + 1);
      setUart0Log("");
      setUart1Log("");
    } catch (err) {
      setCompileLog(String(err));
    }
  }, [lastLoad]);

  // Always toggles, whether or not this line currently resolves to an
  // address - like VSCode, a breakpoint can be set before the program has
  // ever been compiled/run. The sync effect above pushes it to the backend
  // once (if) it resolves.
  const handleToggleBreakpointAtLine = useCallback((file: string, line: number) => {
    const key = bpKey(file, line);
    setBreakpoints((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  }, []);

  const handleRemoveBreakpointKey = useCallback((key: string) => {
    setBreakpoints((prev) => {
      if (!prev.has(key)) return prev;
      const next = new Set(prev);
      next.delete(key);
      return next;
    });
  }, []);

  const handleClearAllBreakpoints = useCallback(() => {
    setBreakpoints(new Set());
  }, []);

  const handleRun = useCallback(() => {
    api.run().then(refreshState);
  }, [refreshState]);
  const handlePause = useCallback(() => {
    api.pause().then(refreshState);
  }, [refreshState]);
  const handleStep = useCallback(() => {
    api.step().then(setSnapshot);
  }, []);

  const handlePinToggle = useCallback((pin: number, level: boolean) => {
    api.setGpioExternal(pin, level).then(refreshState);
  }, [refreshState]);

  const handleUartSend = useCallback((channel: 0 | 1, text: string) => {
    api.feedUart(channel, text + "\n").catch(() => {});
  }, []);

  const handleUartClear = useCallback((channel: 0 | 1) => {
    if (channel === 0) setUart0Log(""); else setUart1Log("");
  }, []);

  const handleChangeFile = useCallback((name: string, content: string) => {
    setProject((p) => (p.files[name] === content ? p : { ...p, files: { ...p.files, [name]: content } }));
  }, []);

  const handleSelectFile = useCallback((name: string) => {
    setProject((p) => (p.activeFile === name ? p : { ...p, activeFile: name }));
  }, []);

  const handleAddFile = useCallback((name: string) => {
    setProject((p) => ({ ...p, files: { ...p.files, [name]: "" }, activeFile: name }));
  }, []);

  const handleRenameFile = useCallback((oldName: string, newName: string) => {
    setProject((p) => {
      if (!(oldName in p.files)) return p;
      const files = { ...p.files, [newName]: p.files[oldName] };
      delete files[oldName];
      return { ...p, files, activeFile: p.activeFile === oldName ? newName : p.activeFile };
    });
  }, []);

  const handleDeleteFile = useCallback((name: string) => {
    setProject((p) => {
      const names = Object.keys(p.files);
      if (names.length <= 1 || !(name in p.files)) return p;
      const files = { ...p.files };
      delete files[name];
      const activeFile = p.activeFile === name ? Object.keys(files)[0] : p.activeFile;
      return { ...p, files, activeFile };
    });
  }, []);

  // Surfaced next to the status pill so "why is it stopped" doesn't require
  // opening the registers panel and cross-referencing pc against lineMap by hand.
  let statusDetail: string | undefined;
  if (snapshot?.status === "fault") {
    statusDetail = snapshot.faultReason;
  } else if (snapshot?.status === "breakpoint") {
    const hit = lineMap.find((la) => la.addr === snapshot.pc);
    statusDetail = hit ? `${hit.file}:${hit.line}` : undefined;
  }

  return (
    <div className="app-shell">
      <DebugToolbar
        status={snapshot?.status ?? "idle"}
        statusDetail={statusDetail}
        loaded={snapshot?.loaded ?? false}
        compiling={compiling}
        compilerAvailable={compilerAvailable}
        mode={mode}
        onModeChange={handleModeChange}
        onCompileAndLoad={handleCompileAndLoad}
        onUpload={handleUpload}
        onRun={handleRun}
        onPause={handlePause}
        onStep={handleStep}
        canReset={lastLoad !== null}
        onReset={handleReset}
      />
      {compileLog && (
        <div className="compile-log">
          <div className="compile-log__header">
            <span>COMPILE LOG</span>
            <button className="icon-btn" onClick={() => setCompileLog("")} title="Dismiss">
              <IconX size={12} />
            </button>
          </div>
          <pre className="compile-log__body">{compileLog}</pre>
        </div>
      )}

      <div className="app-body">
        <div className="main-row" style={{ height: `calc(100% - ${consoleHeight}px)` }}>
          {sidebarOpen && (
            <>
              <aside className="sidebar" style={{ width: sidebarWidth }}>
                <FileExplorer
                  files={Object.keys(files)}
                  activeFile={activeFile}
                  onSelect={handleSelectFile}
                  onAdd={handleAddFile}
                  onRename={handleRenameFile}
                  onDelete={handleDeleteFile}
                />
                <BreakpointsList
                  breakpoints={breakpoints}
                  lineMap={lineMap}
                  onSelectFile={handleSelectFile}
                  onRemove={handleRemoveBreakpointKey}
                  onClearAll={handleClearAllBreakpoints}
                />
              </aside>
              <Resizer direction="col" value={sidebarWidth} min={160} max={480} onDelta={adjustSidebarWidth} label="Resize sidebar" />
            </>
          )}

          <section className="editor-pane">
            <div className="editor-pane__header">
              <button className="icon-btn" onClick={() => setSidebarOpen((v) => !v)} title={sidebarOpen ? "Hide sidebar" : "Show sidebar"}>
                {sidebarOpen ? <IconChevronLeft size={14} /> : <IconChevronRight size={14} />}
              </button>
              <div className="editor-pane__tabs">
                <button
                  className={`editor-pane__tab ${activeTab === "code" ? "editor-pane__tab--active" : ""}`}
                  onClick={() => setActiveTab("code")}
                >
                  Code
                </button>
                <button
                  className={`editor-pane__tab ${activeTab === "circuit" ? "editor-pane__tab--active" : ""}`}
                  onClick={() => setActiveTab("circuit")}
                >
                  Circuit
                </button>
              </div>
              {activeTab === "code" && <span className="editor-pane__filename">{activeFile}</span>}
            </div>
            <div className="editor-pane__body">
              {activeTab === "code" ? (
                <Editor
                  files={files}
                  activeFile={activeFile}
                  onChangeFile={handleChangeFile}
                  lineMap={lineMap}
                  breakpointAddrs={breakpointAddrs}
                  pendingLines={pendingLinesForActiveFile}
                  onToggleBreakpointAtLine={handleToggleBreakpointAtLine}
                  currentPc={snapshot?.pc}
                />
              ) : (
                <CircuitCanvas
                  circuit={circuit}
                  onChange={handleCircuitChange}
                  gpio={snapshot?.gpio ?? []}
                  onGpioPress={handleGpioPress}
                  onGpioRelease={handleGpioRelease}
                  onAdcChange={handleAdcChange}
                />
              )}
            </div>
          </section>

          <Resizer direction="col" value={rightWidth} min={220} max={560} onDelta={(d) => adjustRightWidth(-d)} label="Resize side panel" />
          <aside className="right-panel" style={{ width: rightWidth }}>
            <div className="right-panel__top" style={{ height: registersHeight }}>
              <RegisterView snapshot={snapshot} />
            </div>
            <Resizer direction="row" value={registersHeight} min={100} max={600} onDelta={adjustRegistersHeight} label="Resize registers panel" />
            <div className="right-panel__bottom">
              <PinPanel gpio={snapshot?.gpio ?? []} onToggle={handlePinToggle} />
            </div>
          </aside>
        </div>

        <Resizer direction="row" value={consoleHeight} min={100} max={480} onDelta={(d) => adjustConsoleHeight(-d)} label="Resize console" />
        <div className="console-pane" style={{ height: consoleHeight }}>
          <Console uart0={uart0Log} uart1={uart1Log} onSend={handleUartSend} onClear={handleUartClear} />
        </div>
      </div>
    </div>
  );
}
