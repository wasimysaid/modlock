// The roots of a screen ask for parts of the game's HUD to be hidden and for
// the cursor; the stock guard (guard.ts) hides the parts, and Cursor frees the
// cursor.

import {
  type AbilityRow,
  type HudPart,
  type Screen,
} from '../../src/wasm/ui/ui.pb.js'

/** Dispatch dispatches a Panorama event, as $.DispatchEvent does. */
export type Dispatch = (event: string, ...values: unknown[]) => void

/**
 * Cursor tells the game to free or capture the cursor when the mods' wish
 * changes, and to free it again when a mod asks again: the player can put the
 * cursor away with the game's own keys, which this page is not told, so a
 * screen that changes anything else leaves the player's choice be.
 *
 * The game captures the cursor itself when the local hero arrives, as on
 * joining a server, which drops a free sent before it. So a free waits for the
 * hero and is sent again on each arrival.
 *
 * Capturing the cursor also drops Panorama's input focus within the game's
 * HUD, which holds the mods' panels and the stock HUD they hide. A button or
 * entry the player clicked while the cursor was free keeps the keyboard
 * otherwise, so movement and the camera stay with it until the next click.
 */
export class Cursor {
  /** free is the cursor state the game holds. */
  private free = false

  /** wanted is true while the last screen frees the cursor. */
  private wanted = false

  /** asked is the count of asks for the cursor the last screen held. */
  private asked = 0

  private heroPresent = false

  constructor(
    private readonly dispatch: Dispatch,
    private readonly hud: object,
  ) {}

  /** set frees the cursor or captures it for a screen that holds asks asks. */
  set(free: boolean, asks: number): void {
    // A rise in the count is an ask, which counts even when the cursor is
    // already free here.
    const again = free && asks !== this.asked
    this.asked = asks
    this.wanted = free
    if ((free && !this.heroPresent) || (free === this.free && !again)) return
    this.send(free)
  }

  /** hero tells the cursor whether the local hero is in the game's HUD. */
  hero(present: boolean): void {
    // Track the hero's arrival.
    const arrived = present && !this.heroPresent
    this.heroPresent = present
    if (!arrived) return

    // Free the cursor again if a screen wants it.
    this.free = false
    if (this.wanted) this.send(true)
  }

  private send(free: boolean): void {
    // Hand the keyboard back to the game before it takes the cursor, then
    // tell the game.
    this.free = free
    if (!free) {
      this.dispatch('DropInputFocus', this.hud)
    }
    this.dispatch('CitadelConCommand', `hud_free_cursor ${free ? 1 : 0}`)
  }
}

/**
 * heroInHud reports whether the game's HUD names the local hero's abilities,
 * which it does once the hero's pawn arrives.
 */
export function heroInHud(hud: Panel): boolean {
  return (
    hud
      .FindChildTraverse('hud_signature')
      ?.FindChildrenWithClassTraverse('ability_name')
      .some((label) => label.text !== '') ?? false
  )
}

/** hiddenParts lists the parts of the game's HUD that screen's roots hide. */
export function hiddenParts(screen: Screen | null): HudPart[] {
  const parts: HudPart[] = []
  for (const tree of screen?.trees ?? []) {
    const root = tree.nodes?.find((node) => (node.id ?? '') === '')
    parts.push(...(root?.hideHud ?? []))
  }
  return parts
}

/** cursorFreed reports whether any of screen's roots frees the mouse cursor. */
export function cursorFreed(screen: Screen | null): boolean {
  return (screen?.trees ?? []).some(
    (tree) => tree.nodes?.find((node) => (node.id ?? '') === '')?.freeCursor,
  )
}

/**
 * cursorAsks counts the times the mods behind screen's roots asked for the
 * cursor; it rises when a mod asks again after the player put the cursor away.
 */
export function cursorAsks(screen: Screen | null): number {
  return (screen?.trees ?? []).reduce(
    (total, tree) =>
      total +
      (tree.nodes?.find((node) => (node.id ?? '') === '')?.freeCursorAsks ?? 0),
    0,
  )
}

/** abilityBarMods lists the mods whose roots in screen ask for clicks on the ability bar. */
export function abilityBarMods(screen: Screen | null): string[] {
  return (screen?.trees ?? [])
    .filter(
      (tree) =>
        tree.nodes?.find((node) => (node.id ?? '') === '')?.abilityBarPresses,
    )
    .map((tree) => tree.mod ?? '')
}

/**
 * abilityRows lists the rows screen's roots show on Tab and the ability bar,
 * in the order of their trees.
 */
export function abilityRows(screen: Screen | null): AbilityRow[] {
  return (screen?.trees ?? []).flatMap(
    (tree) =>
      tree.nodes?.find((node) => (node.id ?? '') === '')?.abilityDisplay ?? [],
  )
}
