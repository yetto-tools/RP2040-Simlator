export type ComponentKind = "led" | "button" | "pot" | "buzzer" | "tft7789" | "ili9341" | "oled" | "note";

interface PaletteItem {
  kind: ComponentKind;
  label: string;
}

const AVAILABLE: PaletteItem[] = [
  { kind: "led", label: "LED" },
  { kind: "button", label: "Button" },
  { kind: "pot", label: "Potentiometer" },
  { kind: "buzzer", label: "Buzzer" },
  { kind: "tft7789", label: "TFT (ST7789)" },
  { kind: "ili9341", label: "TFT (ILI9341)" },
  { kind: "oled", label: "OLED (SSD1306)" },
  { kind: "note", label: "+ Note" },
];
const COMING_SOON: string[] = [];

interface Props {
  onAdd: (kind: ComponentKind) => void;
}

export function Palette({ onAdd }: Props) {
  return (
    <div className="circuit-palette">
      <span className="circuit-palette__label">ADD</span>
      {AVAILABLE.map((item) => (
        <button key={item.kind} className="btn btn--ghost circuit-palette__item" onClick={() => onAdd(item.kind)}>
          {item.label}
        </button>
      ))}
      {COMING_SOON.map((label) => (
        <span key={label} className="circuit-palette__item circuit-palette__item--soon" title="Coming soon">
          {label}
        </span>
      ))}
    </div>
  );
}
