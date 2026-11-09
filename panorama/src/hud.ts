// Hud hides parts of the game's own HUD while its host lists them, such as
// the parts mods' roots list, and shows them again once the host stops. The
// game builds some of those panels later, such as a notice for each level up,
// and rebuilds the HUD on its own, and Panorama has no event for a new panel,
// so while any part is hidden the hud looks for its panels again every
// sweepSeconds. It changes them through native changes, one owner per part,
// so it restores the value each panel had, which a layout changing the same
// panel shares.

import { HudPart, type Screen } from '../../src/wasm/ui/ui.pb.js'
import {
  NativeChanges,
  type NativePanel,
  type NativePanorama,
  top,
} from './native.js'

/** HudPanel is the part of a game panel the hud uses. */
export interface HudPanel extends NativePanel {
  FindChildTraverse(id: string): HudPanel | null
  FindChildrenWithClassTraverse(name: string): HudPanel[]
  GetChildCount(): number
  GetChild(index: number): HudPanel | null
  BHasClass(name: string): boolean
  GetParent(): HudPanel | null
}

/** Schedule runs callback once after seconds, as $.Schedule does. */
export type Schedule = (seconds: number, callback: () => void) => void

/** sweepSeconds is how often the hud collapses the parts' new panels. */
const sweepSeconds = 0.1

/**
 * finders returns each part's panels under the game's HUD, as the game's
 * hud.xml and its level-up layout name them.
 */
const finders: Partial<Record<HudPart, (hud: HudPanel) => HudPanel[]>> = {
  [HudPart.LEVEL_UP]: (hud) =>
    hud
      .FindChildTraverse('CitadelHudGameAnnouncements')
      ?.FindChildrenWithClassTraverse('LevelUpBlock') ?? [],
  [HudPart.OWN_HEALTH]: (hud) => {
    const health = hud.FindChildTraverse('health_and_abilities_container')
    return ['HudShieldsContainer', 'hud_health_bars', 'HealthRegenAndTotal']
      .map((id) => health?.FindChildTraverse(id) ?? null)
      .filter((panel) => panel !== null)
  },
  [HudPart.TOP_BAR]: (hud) => {
    const bar = hud.FindChildTraverse('TopBar')
    const parts: HudPanel[] = []
    for (let index = 0; index < (bar?.GetChildCount() ?? 0); index++) {
      const part = bar!.GetChild(index)
      if (part && !part.BHasClass('ChatContainer')) {
        parts.push(part)
      }
    }
    return parts
  },
  [HudPart.AMMO]: (hud) => {
    const ammo = ['out_of_ammo_label', 'low_ammo_label'].map((id) =>
      hud.FindChildTraverse(id),
    )
    const crosshair = hud.FindChildTraverse('crosshair')
    ammo.push(
      ...['clip_status', 'ammo_panel'].map(
        (id) => crosshair?.FindChildTraverse(id) ?? null,
      ),
    )
    return ammo.filter((panel) => panel !== null)
  },
  [HudPart.SOULS]: (hud) => ownPanel(hud, 'gold_and_ap_container'),
  [HudPart.MINIMAP]: (hud) => ownPanel(hud, 'minimap_persp'),
  [HudPart.ABILITIES]: (hud) => ownPanel(hud, 'AbilitiesContainer'),
  [HudPart.STATS]: (hud) => ownPanel(hud, 'hudPlayerStats'),
  // The corner mark sits outside the game's HUD, under the interface's root.
  [HudPart.STATUS]: (hud) => ownPanel(top(hud) as HudPanel, 'DefaultStatus'),
}

/** ownPanel finds the one panel with id under the game's HUD. */
function ownPanel(hud: HudPanel, id: string): HudPanel[] {
  const panel = hud.FindChildTraverse(id)
  return panel ? [panel] : []
}

/** Dispatch dispatches a Panorama event, as $.DispatchEvent does. */
export type Dispatch = (event: string, ...values: unknown[]) => void

/**
 * Cursor tells the game to free or capture the cursor when the mods' wish
 * changes, and to free it again when a mod asks again: the player can put the
 * cursor away with the game's own keys, which this page is not told, so a
 * screen that changes anything else leaves the player's choice be.
 *
 * The game captures the cursor itself when the local hero arrives, as on
 * joining a server, which drops a free sent before it. A free cursor so waits
 * for the hero, and is freed again on each arrival.
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

  /** present is true while the local hero is in the game's HUD. */
  private present = false

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
    if ((free && !this.present) || (free === this.free && !again)) return
    this.send(free)
  }

  /** hero tells the cursor whether the local hero is in the game's HUD. */
  hero(present: boolean): void {
    // Only the hero's arrival changes the game's cursor.
    const arrived = present && !this.present
    this.present = present
    if (!arrived) return

    // The hero's arrival captured the cursor, so a screen that frees it
    // frees it again.
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

/** Hud collapses the parts of the game's HUD it is told to hide. */
export class Hud {
  /** hidden holds the changes of each part some root lists. */
  private hidden = new Map<HudPart, NativeChanges>()

  /** sweeping records that a sweep is scheduled. */
  private sweeping = false

  constructor(
    private readonly hud: HudPanel,
    private readonly schedule: Schedule,
    private readonly panorama: NativePanorama,
  ) {}

  /** draw hides parts and shows every other part it hid again. */
  draw(parts: Iterable<HudPart>): void {
    // Keep the parts the hud knows how to find.
    const hidden = new Map<HudPart, NativeChanges>()
    for (const part of parts) {
      if (finders[part]) {
        hidden.set(
          part,
          this.hidden.get(part) ??
            new NativeChanges(this.panorama, () => this.hud),
        )
      }
    }

    // Show the parts no longer listed, then hide the rest.
    for (const [part, changes] of this.hidden) {
      if (!hidden.has(part)) changes.release()
    }
    this.hidden = hidden
    this.sweep()
  }

  /** sweep collapses each hidden part's panels and schedules the next sweep. */
  private sweep(): void {
    for (const [part, changes] of this.hidden) {
      for (const panel of finders[part]!(this.hud)) {
        if (panel.style.visibility !== 'collapse') changes.hide(panel)
      }
    }
    if (this.hidden.size !== 0 && !this.sweeping) {
      this.sweeping = true
      this.schedule(sweepSeconds, () => {
        this.sweeping = false
        this.sweep()
      })
    }
  }
}
