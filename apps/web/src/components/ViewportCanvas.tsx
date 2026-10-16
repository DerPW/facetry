import { useEffect, useRef } from "react";

import { Viewport } from "../render/viewport";

interface Props {
  onReady: (viewport: Viewport) => void;
  onError: (message: string) => void;
}

/** The WebGL2 canvas and its renderer. Rendering and input run outside React. */
export function ViewportCanvas({ onReady, onError }: Props) {
  const canvas = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    let disposed = false;
    let viewport: Viewport | undefined;
    Viewport.create(element).then(
      (created) => {
        if (disposed) {
          created.dispose();
          return;
        }
        viewport = created;
        onReady(created);
      },
      (error: unknown) => onError(error instanceof Error ? error.message : String(error)),
    );
    return () => {
      disposed = true;
      viewport?.dispose();
    };
  }, [onReady, onError]);

  return <canvas id="viewport" ref={canvas} />;
}
