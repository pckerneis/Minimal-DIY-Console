const { useState, useEffect, useRef } = React;

// ─── Constants ────────────────────────────────────────────────────────────────
const W = 128, H = 64, SCALE = 4;
// Phosphor green palette
const ON  = [74,  246, 38];   // bright pixel
const OFF = [0,   18,  0];    // dark pixel (faint green glow)

// ─── Example Cart ─────────────────────────────────────────────────────────────
const EXAMPLE = `init() {
  bx = 64
  by = 32
  bvx = 1
  bvy = 1
  py = 28
  ey = 28
}

update(frame, input) {
  if (btn(2)) py -= 2
  if (btn(3)) py += 2
  if (py < 0) py = 0
  if (py > 54) py = 54

  bx += bvx
  by += bvy

  if (by < 0 || by > 63) {
    bvy = -bvy
  }

  if (bx < 6 && by >= py && by <= py + 10) {
    bvx = 1
  }

  if (by > ey + 5) ey += 1
  if (by < ey + 5) ey -= 1

  if (bx > 122 && by >= ey && by <= ey + 10) {
    bvx = -1
  }

  if (bx < 0 || bx > 127) {
    bx = 64
    by = 32
    bvx = -bvx
  }
}

draw() {
  cls(0)
  rectfill(2, py, 4, 10, 1)
  rectfill(122, ey, 4, 10, 1)
  rectfill(bx, by, 2, 2, 1)
}`;

// ─── Graphics / Input API factory ─────────────────────────────────────────────
function buildApi(pix, btns, prev) {
  const sp = (x, y, c) => {
    x = x | 0; y = y | 0;
    if (x >= 0 && x < W && y >= 0 && y < H) pix[y * W + x] = c;
  };
  return {
    cls:      c => pix.fill(c ? 1 : 0),
    pset:     (x, y, c) => sp(x, y, c ? 1 : 0),
    rectfill: (x, y, w, h, c) => {
      x=x|0; y=y|0; w=w|0; h=h|0; const v = c ? 1 : 0;
      for (let dy = 0; dy < h; dy++)
        for (let dx = 0; dx < w; dx++) sp(x + dx, y + dy, v);
    },
    line: (x0, y0, x1, y1, c) => {
      x0=x0|0; y0=y0|0; x1=x1|0; y1=y1|0; const v = c ? 1 : 0;
      let dx = Math.abs(x1-x0), dy = -Math.abs(y1-y0);
      let sx = x0<x1?1:-1, sy = y0<y1?1:-1, e = dx+dy;
      for (;;) {
        sp(x0, y0, v);
        if (x0===x1 && y0===y1) break;
        const e2 = 2*e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
      }
    },
    print: (x, y, str) => {
      const s = String(str), tw = s.length * 6 + 2;
      const tmp = document.createElement("canvas");
      tmp.width = tw; tmp.height = 8;
      const ctx = tmp.getContext("2d");
      ctx.fillStyle = "#fff"; ctx.font = "7px monospace"; ctx.textBaseline = "top";
      ctx.fillText(s, 0, 0);
      const d = ctx.getImageData(0, 0, tw, 8).data;
      for (let r = 0; r < 8; r++)
        for (let col = 0; col < tw; col++)
          if (d[(r*tw+col)*4] > 64) sp((x+col)|0, (y+r)|0, 1);
    },
    btn:  i => btns[i|0] ? 1 : 0,
    btnp: i => (btns[i|0] && !prev[i|0]) ? 1 : 0,
  };
}

// ─── Cart Compiler ────────────────────────────────────────────────────────────
// Transforms the MDC cart syntax (implicit vars, bare function defs) into JS.
function compileCart(source, api) {
  const apiKeys = Object.keys(api);
  const skip = new Set([
    ...apiKeys,
    "init","update","draw","audio","frame","input","t",
    "if","else","while","for","return","true","false","null","undefined",
  ]);

  // 1. Collect LHS variable names  (name =, name +=, name -=, …)
  const vars = new Set();
  source.replace(/\b([a-zA-Z_]\w*)\s*(?:[+\-*/%&|^]?=)(?!=)/g, (_, v) => {
    if (!skip.has(v)) vars.add(v);
  });

  // 2. Transform top-level function definitions: `name(params) {` → `function name(params) {`
  const js = source.replace(
    /^(init|update|draw|audio)(\s*\([^)]*\)\s*\{)/gm,
    "function $1$2"
  );

  // 3. Build the factory body
  const body = `
    ${[...vars].map(v => `var ${v}=0;`).join("\n")}
    ${js}
    return {
      init:   typeof init   === "function" ? init   : function(){},
      update: typeof update === "function" ? update : function(){},
      draw:   typeof draw   === "function" ? draw   : function(){},
    };
  `;

  // eslint-disable-next-line no-new-func
  return new Function(...apiKeys, body)(...Object.values(api));
}

// ─── Main Component ───────────────────────────────────────────────────────────
export default function MDCEmulator() {
  const canvasRef = useRef(null);
  const [code, setCode]       = useState(EXAMPLE);
  const [running, setRunning] = useState(false);
  const [error, setError]     = useState("");

  // Pixel buffer and button state — mutable, never triggers re-render
  const pix   = useRef(new Uint8Array(W * H)).current;
  const btns  = useRef(new Array(6).fill(0)).current;
  const prev  = useRef(new Array(6).fill(0)).current;
  const fn    = useRef(0);       // frame number
  const cart  = useRef(null);

  // Build API once
  const api = useRef(null);
  if (!api.current) api.current = buildApi(pix, btns, prev);

  // ── Blit pixel buffer to canvas ──────────────────────────────────────────
  const blit = () => {
    const cv = canvasRef.current; if (!cv) return;
    const ctx = cv.getContext("2d");
    const img = ctx.createImageData(W * SCALE, H * SCALE);
    const d   = img.data;
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        const on = pix[y * W + x];
        const [R, G, B] = on ? ON : OFF;
        for (let sy = 0; sy < SCALE; sy++) {
          for (let sx = 0; sx < SCALE; sx++) {
            const i = ((y*SCALE+sy)*W*SCALE + (x*SCALE+sx)) * 4;
            d[i]=R; d[i+1]=G; d[i+2]=B; d[i+3]=255;
          }
        }
      }
    }
    ctx.putImageData(img, 0, 0);
  };

  // ── Start / Stop ─────────────────────────────────────────────────────────
  const doStart = () => {
    setError("");
    try {
      const loaded = compileCart(code, api.current);
      loaded.init();
      cart.current = loaded;
      fn.current   = 0;
      blit();
      setRunning(true);
    } catch (e) {
      setError(e.message || String(e));
    }
  };
  const doStop = () => { setRunning(false); cart.current = null; };

  // ── 30 fps game loop ──────────────────────────────────────────────────────
  useEffect(() => {
    if (!running) return;
    const id = setInterval(() => {
      for (let i = 0; i < 6; i++) prev[i] = btns[i];
      try {
        cart.current?.update(fn.current, 0);
        cart.current?.draw(fn.current, 0);
        blit();
        fn.current++;
      } catch (e) {
        setError(e.message || String(e));
        setRunning(false);
      }
    }, 1000 / 30);
    return () => clearInterval(id);
  }, [running]);

  // ── Keyboard input ────────────────────────────────────────────────────────
  useEffect(() => {
    const map = { ArrowLeft:0, ArrowRight:1, ArrowUp:2, ArrowDown:3, z:4, x:5, Z:4, X:5 };
    const kd = e => { const i = map[e.key]; if (i !== undefined) { btns[i]=1; e.preventDefault(); } };
    const ku = e => { const i = map[e.key]; if (i !== undefined) btns[i]=0; };
    window.addEventListener("keydown", kd);
    window.addEventListener("keyup",   ku);
    return () => { window.removeEventListener("keydown", kd); window.removeEventListener("keyup", ku); };
  }, []);

  // ── On-screen button event handlers ──────────────────────────────────────
  const ph = i => ({
    onMouseDown:  e => { btns[i]=1; e.preventDefault(); },
    onMouseUp:    () => { btns[i]=0; },
    onMouseLeave: () => { btns[i]=0; },
    onTouchStart: e => { btns[i]=1; e.preventDefault(); },
    onTouchEnd:   () => { btns[i]=0; },
    onTouchCancel:() => { btns[i]=0; },
  });

  // ── Styles ────────────────────────────────────────────────────────────────
  const FONT = "'Courier New', Courier, monospace";

  const s = {
    root: {
      fontFamily: FONT,
      background: "linear-gradient(160deg, #0a0a14 0%, #080810 100%)",
      minHeight: "100vh",
      padding: "24px 16px 40px",
      display: "flex",
      flexDirection: "column",
      alignItems: "center",
      gap: 20,
      boxSizing: "border-box",
    },
    shell: {
      background: "linear-gradient(175deg, #222238 0%, #181828 60%, #141422 100%)",
      borderRadius: 22,
      padding: "20px 20px 26px",
      maxWidth: 560,
      width: "100%",
      boxShadow: [
        "0 24px 64px rgba(0,0,0,0.95)",
        "inset 0 1px 0 rgba(255,255,255,0.06)",
        "inset 0 -3px 0 rgba(0,0,0,0.4)",
        "0 0 0 1px rgba(255,255,255,0.03)",
      ].join(","),
      display: "flex",
      flexDirection: "column",
      gap: 16,
    },
    topBar: {
      display: "flex",
      justifyContent: "space-between",
      alignItems: "center",
    },
    brand: {
      color: "#2e2e52",
      fontSize: 9,
      letterSpacing: 4,
      textTransform: "uppercase",
    },
    dots: { display: "flex", gap: 5 },
    bezel: {
      background: "#0a0a10",
      borderRadius: 10,
      padding: "10px 14px",
      boxShadow: [
        "inset 0 4px 14px rgba(0,0,0,0.98)",
        "inset 0 0 0 1px rgba(255,255,255,0.02)",
      ].join(","),
    },
    screenWrap: {
      position: "relative",
      background: "#001200",
      borderRadius: 4,
      overflow: "hidden",
      lineHeight: 0,
    },
    canvas: {
      display: "block",
      width: "100%",
      imageRendering: "pixelated",
    },
    scanlines: {
      position: "absolute", inset: 0, pointerEvents: "none",
      background: "repeating-linear-gradient(to bottom, transparent 0px, transparent 2px, rgba(0,0,0,0.18) 2px, rgba(0,0,0,0.18) 4px)",
    },
    screenGlow: {
      position: "absolute", inset: -2, pointerEvents: "none",
      boxShadow: "inset 0 0 20px rgba(74,246,38,0.06)",
    },
    controls: {
      display: "flex",
      justifyContent: "space-between",
      alignItems: "center",
      paddingInline: 4,
    },
    dpad: {
      display: "grid",
      gridTemplateColumns: "repeat(3, 42px)",
      gridTemplateRows: "repeat(3, 42px)",
      gap: 3,
    },
    dBtn: {
      width: 42, height: 42,
      background: "linear-gradient(160deg, #2c2c4c, #1e1e34)",
      border: "1px solid #3a3a5e",
      borderRadius: 5,
      color: "#7070a0",
      fontSize: 15,
      cursor: "pointer",
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      userSelect: "none",
      WebkitUserSelect: "none",
      outline: "none",
      padding: 0,
      fontFamily: FONT,
      boxShadow: "0 3px 0 #0a0a18, inset 0 1px 0 rgba(255,255,255,0.05)",
      boxSizing: "border-box",
    },
    dpadNub: {
      width: 42, height: 42,
      background: "#14142a",
      border: "1px solid #2a2a46",
      borderRadius: 4,
      boxShadow: "inset 0 1px 3px rgba(0,0,0,0.8)",
    },
    centerLabel: {
      color: "#1e1e38",
      fontSize: 9,
      letterSpacing: 3,
      textAlign: "center",
      flexShrink: 0,
    },
    divider: {
      width: 1, height: 14, background: "#1e1e38",
      margin: "0 auto",
    },
    abGroup: {
      display: "flex",
      gap: 10,
      alignItems: "flex-end",
    },
    aBtn: {
      width: 46, height: 46,
      borderRadius: "50%",
      background: "radial-gradient(circle at 40% 35%, #3a1e42, #220f28)",
      border: "2px solid #6a3a7a",
      color: "#c088cc",
      fontSize: 13,
      fontWeight: "bold",
      cursor: "pointer",
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      userSelect: "none",
      WebkitUserSelect: "none",
      outline: "none",
      fontFamily: FONT,
      boxShadow: "0 4px 0 rgba(0,0,0,0.7), inset 0 1px 0 rgba(255,255,255,0.08)",
    },
    bBtn: {
      width: 46, height: 46,
      borderRadius: "50%",
      background: "radial-gradient(circle at 40% 35%, #1e1e42, #0f0f28)",
      border: "2px solid #3a3a72",
      color: "#8888cc",
      fontSize: 13,
      fontWeight: "bold",
      cursor: "pointer",
      display: "flex",
      alignItems: "center",
      justifyContent: "center",
      userSelect: "none",
      WebkitUserSelect: "none",
      outline: "none",
      fontFamily: FONT,
      boxShadow: "0 4px 0 rgba(0,0,0,0.7), inset 0 1px 0 rgba(255,255,255,0.06)",
      marginBottom: 18,
    },
    editorSection: {
      maxWidth: 560,
      width: "100%",
      display: "flex",
      flexDirection: "column",
      gap: 8,
    },
    sectionLabel: {
      color: "#2a2a4a",
      fontSize: 9,
      letterSpacing: 4,
      textTransform: "uppercase",
    },
    textarea: {
      width: "100%",
      height: 280,
      boxSizing: "border-box",
      background: "#090914",
      color: "#52d452",
      border: "1px solid #1e1e38",
      borderRadius: 8,
      padding: "12px 14px",
      fontFamily: FONT,
      fontSize: 13,
      lineHeight: 1.7,
      resize: "vertical",
      outline: "none",
      tabSize: 2,
      caretColor: "#74f626",
    },
    toolbar: {
      display: "flex",
      gap: 10,
      alignItems: "center",
      flexWrap: "wrap",
    },
    runBtn: (isRunning) => ({
      padding: "9px 22px",
      borderRadius: 7,
      fontFamily: FONT,
      fontSize: 12,
      fontWeight: "bold",
      letterSpacing: 2,
      cursor: "pointer",
      outline: "none",
      border: `1px solid ${isRunning ? "#6a1a1a" : "#1a5a1a"}`,
      background: isRunning
        ? "linear-gradient(160deg, #280c0c, #1a0808)"
        : "linear-gradient(160deg, #0c280c, #081808)",
      color: isRunning ? "#ff6060" : "#60ff60",
      boxShadow: `0 2px 8px ${isRunning ? "rgba(255,60,60,0.15)" : "rgba(60,255,60,0.15)"}`,
    }),
    errorBox: {
      color: "#ff5555",
      fontSize: 11,
      flex: 1,
      lineHeight: 1.4,
    },
    hint: {
      color: "#1e1e3a",
      fontSize: 10,
      lineHeight: 1.6,
    },
  };

  // D-pad layout: [label, btnIndex] for 9 cells (3×3 grid)
  const dpadCells = [
    null,       [2, "▲"], null,
    [0, "◀"],   null,     [1, "▶"],
    null,       [3, "▼"], null,
  ];

  return (
    <div style={s.root}>

      {/* ── Console Shell ── */}
      <div style={s.shell}>

        {/* Top bar */}
        <div style={s.topBar}>
          <span style={s.brand}>Minimal DIY Console</span>
          <div style={s.dots}>
            {["#ff5555","#ffaa33","#44cc44"].map((c, i) => (
              <div key={i} style={{ width:7, height:7, borderRadius:"50%", background:c, opacity:.22 }} />
            ))}
          </div>
        </div>

        {/* Screen bezel + canvas */}
        <div style={s.bezel}>
          <div style={s.screenWrap}>
            <canvas
              ref={canvasRef}
              width={W * SCALE}
              height={H * SCALE}
              style={s.canvas}
            />
            <div style={s.scanlines} />
            <div style={s.screenGlow} />
          </div>
        </div>

        {/* Controls row */}
        <div style={s.controls}>

          {/* D-pad */}
          <div style={s.dpad}>
            {dpadCells.map((cell, i) =>
              cell
                ? <button key={i} style={s.dBtn} {...ph(cell[0])}>{cell[1]}</button>
                : i === 4
                  ? <div key={i} style={s.dpadNub} />
                  : <div key={i} />
            )}
          </div>

          {/* MDC wordmark */}
          <div style={s.centerLabel}>
            <div style={s.divider} />
            <div style={{ margin:"6px 0", letterSpacing:3 }}>MDC</div>
            <div style={s.divider} />
          </div>

          {/* A/B buttons */}
          <div style={s.abGroup}>
            <button style={s.bBtn} {...ph(5)}>B</button>
            <button style={s.aBtn} {...ph(4)}>A</button>
          </div>

        </div>
      </div>

      {/* ── Editor ── */}
      <div style={s.editorSection}>
        <span style={s.sectionLabel}>Cart Program</span>
        <textarea
          value={code}
          onChange={e => setCode(e.target.value)}
          spellCheck={false}
          style={s.textarea}
        />
        <div style={s.toolbar}>
          <button style={s.runBtn(running)} onClick={running ? doStop : doStart}>
            {running ? "■  STOP" : "▶  RUN"}
          </button>
          {error && <span style={s.errorBox}>{error}</span>}
        </div>
        <span style={s.hint}>
          Keyboard: ↑↓←→ move · Z = A · X = B · or click the on-screen buttons
        </span>
      </div>

    </div>
  );
}
