// A screen starts when it first holds a tree and ends when it holds none, as
// when the stream closes, the player leaves the server or the server stops.
// The main HUD panels are kept before the screen's first layouts load and put
// back after its last layouts unload and the hud shows its parts again, so a
// layout that wrote to them directly cannot leave them changed.

import type { Screen } from '../../src/wasm/ui/ui.pb.js'
import type { AddonLayouts, Drawing } from './draw.js'
import {
  type Cursor,
  type Hud,
  type HudPanel,
  cursorAsks,
  cursorFreed,
  hiddenParts,
  snapshotHud,
} from './hud.js'

/** presenter returns the function that shows each screen the bridge reads. */
export function presenter(
  game: HudPanel,
  layouts: AddonLayouts,
  drawing: Drawing,
  stock: Hud,
  cursor: Cursor,
): (screen: Screen | null) => void {
  let restore: (() => void) | null = null
  return (screen) => {
    // Keep the HUD as a starting screen finds it, draw, and restore it once
    // the screen ends and its layouts have released their changes.
    const showing = (screen?.trees?.length ?? 0) !== 0
    if (showing) restore ??= snapshotHud(game)
    layouts.follow(screen)
    drawing.draw(screen)
    stock.draw(hiddenParts(screen))
    cursor.set(cursorFreed(screen), cursorAsks(screen))
    if (!showing && restore) {
      restore()
      restore = null
    }
  }
}
