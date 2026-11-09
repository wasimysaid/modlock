// Native changes are the one way the renderer and mods' layouts change the
// game's own panels. The first owner to change a property saves its original
// on the panel, so every owner in every script context restores the same
// value, and the last owner to release a property restores it.
//
// In Panorama an inline visibility sets a panel's visible, clearing it
// applies only the game's own CSS, and setting visible writes the inline
// visibility. So visible is kept as style.visibility, and a restore turns
// visible on before it writes the original, so a cleared style leaves the
// panel to the game's CSS instead of hidden.

/** NativePanel is the part of a game panel native changes use. */
export interface NativePanel {
  readonly style: Record<string, string | null>
  visible: boolean
  hittest: boolean
  hittestchildren: boolean
  text: string
  IsValid(): boolean
  GetParent(): NativePanel | null
  FindChildTraverse(id: string): NativePanel | null
  GetAttributeString(name: string, fallback: string): string
  SetAttributeString(name: string, value: string): void
  DeleteAsync(seconds: number): void
  MoveChildBefore(child: NativePanel, before: NativePanel): void
  MoveChildAfter(child: NativePanel, after: NativePanel): void
}

/** NativePanorama is the part of the game's script API native changes use. */
export interface NativePanorama {
  CreatePanel(type: string, parent: NativePanel, id: string): NativePanel
  Localize(token: string, panel: NativePanel): string
}

/**
 * NativeTarget is a panel, or the id of one under the interface's root; a
 * missing panel changes nothing.
 */
export type NativeTarget = NativePanel | string | null | undefined

/** NativeFlags are the panel fields native changes set. */
export interface NativeFlags {
  visible?: boolean
  hittest?: boolean
  hittestchildren?: boolean
}

/**
 * Placement puts a created panel just before or after one of its parent's
 * children; without one it goes last.
 */
export type Placement = { before: NativeTarget } | { after: NativeTarget }

/** TextRestore restores a label's text as the game localizes it. */
export interface TextRestore {
  token: string
  scope?: NativePanel
}

/** Flag names a hit-testing field; visible is style.visibility's to keep. */
type Flag = 'hittest' | 'hittestchildren'

/** Key names a style property as `style.<name>`, or a panel field. */
type Key = `style.${string}` | Flag | 'text'

type Value = string | boolean | null

/** Saved is a property's original and the owners holding the property. */
interface Saved {
  original: Value
  owners: string[]
}

/** savedAttribute names the panel attribute that holds its saved originals. */
export const savedAttribute = 'modlock_native'

/** NativeChanges is one owner's changes to the game's panels. */
export class NativeChanges {
  private readonly owner = `${Date.now().toString(36)}.${Math.random().toString(36).slice(2)}`
  private readonly held = new Map<NativePanel, Set<Key>>()
  private readonly restores = new Map<NativePanel, TextRestore>()
  private readonly created: NativePanel[] = []

  constructor(
    private readonly panorama: NativePanorama,
    private readonly root: () => NativePanel,
  ) {}

  style(
    target: NativeTarget,
    values: Record<string, string | null>,
  ): NativePanel | null {
    return this.write(
      target,
      Object.entries(values).map(([name, value]) => [`style.${name}`, value]),
    )
  }

  set(target: NativeTarget, values: NativeFlags): NativePanel | null {
    const entries: [Key, Value][] = []
    if (values.visible !== undefined)
      entries.push([
        'style.visibility',
        values.visible ? 'visible' : 'collapse',
      ])
    for (const key of ['hittest', 'hittestchildren'] satisfies Flag[]) {
      if (values[key] !== undefined) entries.push([key, values[key]])
    }
    return this.write(target, entries)
  }

  hide(target: NativeTarget): NativePanel | null {
    return this.style(target, { visibility: 'collapse' })
  }

  /**
   * text sets a label's text; release restores the saved text, or with
   * restore the game's localized text, for a label the game rewrites.
   */
  text(
    target: NativeTarget,
    value: string,
    restore?: TextRestore,
  ): NativePanel | null {
    const panel = this.write(target, [['text', value]])
    if (panel && restore) this.restores.set(panel, restore)
    return panel
  }

  /**
   * create creates a panel inside a native panel, at placement among its
   * children, which release deletes. Only the created panel moves.
   */
  create(
    type: string,
    parent: NativeTarget,
    id: string,
    placement?: Placement,
  ): NativePanel | null {
    // Make the panel inside its parent, then move it into place.
    const inside = this.find(parent)
    if (!inside) return null
    const panel = this.panorama.CreatePanel(type, inside, id)
    this.created.push(panel)
    if (placement && 'before' in placement) {
      const sibling = this.sibling(inside, placement.before)
      if (sibling) inside.MoveChildBefore(panel, sibling)
    } else if (placement) {
      const sibling = this.sibling(inside, placement.after)
      if (sibling) inside.MoveChildAfter(panel, sibling)
    }
    return panel
  }

  /**
   * release restores each property no other owner holds and deletes the
   * panels this owner created.
   */
  release(): void {
    // Restore what no other owner holds, then delete what this one made.
    for (const [panel, keys] of this.held) {
      if (!panel.IsValid()) continue
      const saved = readSaved(panel)
      for (const key of keys) {
        const entry = saved[key]
        if (!entry) continue
        entry.owners = entry.owners.filter((owner) => owner !== this.owner)
        if (entry.owners.length !== 0) continue
        const restore = key === 'text' ? this.restores.get(panel) : undefined
        if (key === 'style.visibility') panel.visible = true
        write(
          panel,
          key,
          restore
            ? this.panorama.Localize(restore.token, restore.scope ?? panel)
            : entry.original,
        )
        delete saved[key]
      }
      writeSaved(panel, saved)
    }
    for (const panel of this.created) {
      if (panel.IsValid()) panel.DeleteAsync(0)
    }
    this.held.clear()
    this.restores.clear()
    this.created.length = 0
  }

  private sibling(
    parent: NativePanel,
    target: NativeTarget,
  ): NativePanel | null {
    const panel =
      typeof target === 'string' ? parent.FindChildTraverse(target) : target
    return panel?.IsValid() && panel.GetParent() === parent ? panel : null
  }

  private find(target: NativeTarget): NativePanel | null {
    if (typeof target === 'string')
      return top(this.root()).FindChildTraverse(target)
    return target?.IsValid() ? target : null
  }

  private write(
    target: NativeTarget,
    values: [Key, Value][],
  ): NativePanel | null {
    // Join the owners of each property this owner did not hold yet, saving
    // the original if none did, then write the values.
    const panel = this.find(target)
    if (!panel) return null
    let keys = this.held.get(panel)
    if (!keys) this.held.set(panel, (keys = new Set()))
    const fresh = values.filter(([key]) => !keys.has(key))
    if (fresh.length !== 0) {
      const saved = readSaved(panel)
      for (const [key] of fresh) {
        const entry = (saved[key] ??= {
          original: read(panel, key),
          owners: [],
        })
        entry.owners.push(this.owner)
        keys.add(key)
      }
      writeSaved(panel, saved)
    }
    for (const [key, value] of values) write(panel, key, value)
    return panel
  }
}

/** top returns the root of the interface panel sits in. */
export function top(panel: NativePanel): NativePanel {
  let root = panel
  for (let up = root.GetParent(); up; up = up.GetParent()) root = up
  return root
}

function read(panel: NativePanel, key: Key): Value {
  if (key.startsWith('style.')) return panel.style[key.slice(6)] ?? null
  return panel[key as Flag | 'text']
}

function write(panel: NativePanel, key: Key, value: Value): void {
  if (key.startsWith('style.')) {
    panel.style[key.slice(6)] = value as string | null
  } else if (key === 'text') {
    panel.text = value as string
  } else {
    panel[key as Flag] = value as boolean
  }
}

function readSaved(panel: NativePanel): Partial<Record<Key, Saved>> {
  const text = panel.GetAttributeString(savedAttribute, '')
  return text === '' ? {} : (JSON.parse(text) as Partial<Record<Key, Saved>>)
}

function writeSaved(panel: NativePanel, saved: Partial<Record<Key, Saved>>) {
  panel.SetAttributeString(
    savedAttribute,
    Object.keys(saved).length === 0 ? '' : JSON.stringify(saved),
  )
}
