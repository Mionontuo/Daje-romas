const fs = require("fs");

const path = process.argv[2];
if (!path) throw new Error("Dashboard path is required.");
const html = fs.readFileSync(path, "utf8");
const start = html.indexOf("<script>");
const end = html.lastIndexOf("</script>");
if (start < 0 || end <= start) throw new Error("Inline dashboard script not found.");

new Function(html.slice(start + "<script>".length, end));
for (const invariant of [
  "127.0.0.1:8765/telemetry",
  "127.0.0.1:8765/trace?cylinder=",
  "127.0.0.1:8765/control",
  'id="liveButton"',
  'id="modeBadge"',
  'id="remoteBrake"',
  'id="remoteLimiter"',
  'id="customChart"',
  'id="exportCsv"',
  'id="exportPng"'
]) {
  if (!html.includes(invariant)) throw new Error(`Missing dashboard invariant: ${invariant}`);
}
if (/<(?:script|link)[^>]+https?:\/\//i.test(html)) {
  throw new Error("The dashboard must not load remote script or stylesheet code.");
}
console.log("Dashboard JavaScript and live telemetry invariants passed.");
