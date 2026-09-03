import { useEffect, useRef, useState } from "react";
import { IconSend, IconTrash } from "../icons";

interface Props {
  uart0: string;
  uart1: string;
  onSend: (channel: 0 | 1, text: string) => void;
  onClear: (channel: 0 | 1) => void;
}

export function Console({ uart0, uart1, onSend, onClear }: Props) {
  const [channel, setChannel] = useState<0 | 1>(0);
  const [input, setInput] = useState("");
  const bodyRef = useRef<HTMLPreElement>(null);
  const output = channel === 0 ? uart0 : uart1;

  useEffect(() => {
    const el = bodyRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [output]);

  const send = () => {
    if (!input) return;
    onSend(channel, input);
    setInput("");
  };

  return (
    <div className="console">
      <div className="console-tabs">
        {([0, 1] as const).map((n) => (
          <button
            key={n}
            className={`console-tabs__tab ${channel === n ? "console-tabs__tab--active" : ""}`}
            onClick={() => setChannel(n)}
          >
            UART{n}
          </button>
        ))}
        <div className="console-tabs__spacer" />
        <button
          className="icon-btn"
          onClick={() => onClear(channel)}
          disabled={!output}
          title={`Clear UART${channel} log`}
        >
          <IconTrash size={13} />
        </button>
      </div>
      <pre ref={bodyRef} className="console__body">
        {output}
      </pre>
      <div className="console__input-row">
        <input
          id={`uart-${channel}-input`}
          name={`uart-${channel}-input`}
          className="console__input"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") send();
          }}
          placeholder={`Send to UART${channel}...`}
          aria-label={`Send to UART${channel}`}
        />
        <button className="console__send" onClick={send} title="Send">
          <IconSend size={14} />
        </button>
      </div>
    </div>
  );
}
