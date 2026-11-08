// The data contract between the renderer and a mod's layout scripts: the
// attributes of a layout's panel that hold its data and the mod's routes, and
// the event that announces a change. The renderer writes them and the layout
// library reads them, so only these two change when the contract does.

/** layoutDataAttribute names the attribute holding a layout node's data. */
export const layoutDataAttribute = 'modlock_data'

/** layoutUnloading marks a layout before its panel or an ancestor leaves. */
export const layoutUnloading = 'modlock_unloading'

/**
 * routeAttribute names the attribute of each layout's panel that holds the
 * route name's value, empty when the mod sends none.
 */
export function routeAttribute(name: string): string {
  return `modlock_route_${name}`
}

/**
 * layoutDataChanged is the sound event the drawing plays after a draw that
 * changes a layout's data or a route. The game refuses custom events and
 * delivers the stock PlaySoundEffect to every script; no sound has this
 * name, so none plays.
 */
export const layoutDataChanged = 'modlock_data_changed'

/**
 * isRouteName reports whether name names a route: one or more letters,
 * digits, '_', '-' and '.', so it fits in an attribute's name.
 */
export function isRouteName(name: string): boolean {
  return /^[\w.-]+$/.test(name)
}
