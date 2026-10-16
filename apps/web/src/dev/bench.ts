// Development only (see vite.config.ts): measures draw and pick times and posts them to the dev
// server, which prints them to its log. Opened with ?stress&bench.

import type { Viewport } from "../render/viewport";

export async function postBenchmark(
  viewport: Viewport,
  canvas: HTMLCanvasElement,
  triangles: number,
) {
  viewport.benchmark(10);
  const frameMs = [viewport.benchmark(60), viewport.benchmark(60), viewport.benchmark(60)];
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const started = performance.now();
  for (let i = 0; i < 200; i++) {
    viewport.pick((((i * 37) % 97) / 97) * width, (((i * 53) % 89) / 89) * height, 18);
  }
  const pickMs = (performance.now() - started) / 200;
  // Frames actually presented in two seconds while rendering continuously.
  const frames = await new Promise<number>((resolve) => {
    let count = 0;
    const until = performance.now() + 2000;
    const tick = () => {
      count++;
      if (performance.now() < until) requestAnimationFrame(tick);
      else resolve(count);
    };
    requestAnimationFrame(tick);
  });
  await fetch("/__bench", {
    method: "POST",
    body: JSON.stringify({
      userAgent: navigator.userAgent,
      triangles,
      canvas: [canvas.width, canvas.height],
      frameMs: frameMs.map((ms) => Number(ms.toFixed(2))),
      pickMs: Number(pickMs.toFixed(3)),
      presentedFps: frames / 2,
    }),
  });
}
