import { ImageResponse } from "next/og";

export const runtime = "edge";
export const alt = "FSOC — AI-Based Virtual Camera Tracking";
export const size = { width: 1200, height: 630 };
export const contentType = "image/png";

const BG = "#0e1514";
const BORDER = "#3c4947";
const PRIMARY = "#6feee1";
const TEXT = "#dee4e2";
const MUTED = "#869491";
const WARNING = "#ffd2a2";

export default async function Image() {
  return new ImageResponse(
    (
      <div
        style={{
          width: "100%",
          height: "100%",
          display: "flex",
          flexDirection: "column",
          justifyContent: "space-between",
          background: BG,
          padding: 64,
          fontFamily: "monospace",
        }}
      >
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 12,
              color: MUTED,
              fontSize: 20,
              letterSpacing: 4,
              textTransform: "uppercase",
            }}
          >
            <div style={{ display: "flex", width: 10, height: 10, border: `2px solid ${PRIMARY}` }} />
            SIH26169 &middot; TEAM IRODOV
          </div>
          <div style={{ display: "flex", color: MUTED, fontSize: 20, letterSpacing: 2 }}>
            github.com/ThatKJ/FSOC
          </div>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 20 }}>
          <div style={{ display: "flex", color: TEXT, fontSize: 108, fontWeight: 700, letterSpacing: -2 }}>
            FSOC
          </div>
          <div style={{ display: "flex", color: TEXT, fontSize: 34, maxWidth: 900 }}>
            AI-Based Virtual Camera Tracking
          </div>
          <div style={{ display: "flex", color: MUTED, fontSize: 24, maxWidth: 900 }}>
            for Mobile Free-Space Optical Communication
          </div>
        </div>

        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 18,
            borderTop: `1px solid ${BORDER}`,
            paddingTop: 32,
            fontSize: 26,
            letterSpacing: 3,
            textTransform: "uppercase",
          }}
        >
          <span style={{ display: "flex", color: PRIMARY }}>SEE</span>
          <span style={{ display: "flex", color: MUTED }}>&#8594;</span>
          <span style={{ display: "flex", color: PRIMARY }}>ESTIMATE</span>
          <span style={{ display: "flex", color: MUTED }}>&#8594;</span>
          <span style={{ display: "flex", color: WARNING }}>PREDICT</span>
          <span style={{ display: "flex", color: MUTED }}>&#8594;</span>
          <span style={{ display: "flex", color: WARNING }}>CORRECT</span>
        </div>
      </div>
    ),
    { ...size },
  );
}
