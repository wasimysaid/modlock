# Hideout fork

The `hideout` branch keeps the runtime extensions needed by Hideout-hosted modes
on top of upstream Modlock. Bebop Wars is a consumer, not a dependency of this
fork. Mode rules, currencies, UI adapters and research commands belong in the
mode project.

This branch includes upstream through `d54df29` and retains the two original
Bebop commits, `b725809` and `5e63fed`. The `29ae444-bebop.2` release tag remains
unchanged. A branch update is not an update to an installed runtime: build the
CLI, generated libraries, renderer and Windows host from the same revision
before adopting it.

## Native integration

Modlock's native host reads and modifies loaded game data and hooks engine
functions. Definition changes are memory changes, not edits to Valve's files
on disk. They depend on the installed game's native layout. Schema checks and
bounded memory operations reject incompatible accesses; they do not turn this
into an official Valve scripting API or establish matchmaking compatibility.

The existing definition lifecycle keeps original values and restores them when
the mod unloads while the game module is still loaded. Server definition edits
do not automatically update client tooltip metadata or establish correct client
prediction. See [the test findings](hideout-testing.md) for observed behavior and
remaining checks.

## Retained extensions

| Extension | Owner | Contract |
| --- | --- | --- |
| Modifier float definitions and readback | Native definition layer | Direct schema-checked float fields on modifier definitions; no pointer traversal or copies |
| Scripted press bypass and cancellation | Native input layer | A queued press bypasses blocking for one input; cancellation removes undelivered queued buttons |
| Packaged image dimensions | Panorama content compiler | Stylesheet image tables use dimensions from packaged VTEX headers |
| Numeric native tier bonuses | Native definition layer | Replace values of existing ability-upgrade rows; preserve native costs and row identity |
| Layout unload callbacks | Panorama renderer and layout library | `Modlock.onUnload(handler)` runs once before a layout or its ancestor is removed or replaced; use it to cancel work |
| Native panel changes | Panorama renderer and layout library | `Modlock.native` is the one way a layout changes the game's own panels; every change is released when the layout unloads, and a released visibility goes back to the game's own CSS |
| Session commands | Relay and renderer | The relay stream carries `connect` to a loopback port and `disconnect`, which the renderer runs as console commands |
| Pawn observer service fields | Native entity field layer | Existing field reads and writes resolve `CPlayer_ObserverServices` through a pawn's schema-declared observer services pointer |
| Server-only mode settings | Native console layer | A console variable a mode sets reaches the player only if the game puts it back when the player leaves |

A scripted press bypasses the combined input block mask, including blocks from
other loaded mods. It does not remove those blocks or change the physical input
edges reported to watchers. Cancellation cannot retract a press already sent to
the game. Modes must cancel pending actions when their owner leaves or dies.

Image dimensions are a compiler fix. External game images retain upstream
lookup behavior; malformed packaged textures fail compilation. This does not
replace the asset SDK or change image pixels.

Layout unload callbacks cover UI removal, layout replacement and mod reload.
They run while the layout's panel is still valid. A failed callback is logged
and does not prevent the remaining callbacks. They do not run when the entire
game process or its Panorama context is destroyed.

Observer service fields use the existing `readField` and `writeField` API. The
host verifies `CBasePlayerPawn` ancestry before reading `m_pObserverServices`;
field storage/type checks and write replication follow the ordinary field path.
This extension does not select spectator targets or repair client camera state.
Consumers can require the packaging capability `observer-service-fields-v1` when
adopting this runtime revision; capability metadata belongs to their packaging.

A client keeps a replicated console variable it got from a server after it
leaves, unless the variable carries the `release` flag, and its own menu and
sandbox servers then run with the mode's value: zero kill rewards after Bebop
Wars, no zipline start after any mode built on `arena`. So `serverCommand`
clears `replicated` from such a variable before it sets it, and only the
server has the mode's value. Variables with `release`, such as
`citadel_koth_enabled` and `sv_cheats`, still replicate.

## Changing the game's HUD

A layout changes the game's own panels only through `Modlock.native`. It never
writes a native panel's style or fields itself:

```ts
type NativeTarget = Panel | string | null | undefined // an id is found from the interface's root

interface NativeChanges {
  style(target: NativeTarget, values: { [property: string]: string | null }): Panel | null
  set(target: NativeTarget, values: { visible?: boolean; hittest?: boolean; hittestchildren?: boolean }): Panel | null
  hide(target: NativeTarget): Panel | null
  create(type: string, parent: NativeTarget, id: string, placement?: { before: NativeTarget } | { after: NativeTarget }): Panel | null
  relabel(target: NativeTarget, text: string, classes?: string[]): Panel | null
  release(): void
}

Modlock.native: NativeChanges & { group(): NativeChanges }
```

Each call returns the panel it changed or created. A missing target, null or
an id that finds nothing changes nothing and returns null, so a layout may pass
`FindChildTraverse` results straight in.

The first owner to change a property saves the panel's current value on the
panel itself. Every owner restores that one original: the renderer's `hideHud`,
two instances of a layout during a reload, and the groups of any layout.
Changing a property again keeps the first original, so a layout may re-apply
its changes on every sweep. A panel the game rebuilds is a new panel with its
own original. `release()` hands back an owner's changes. A property returns to
its original once no owner holds it, and the panels the owner created are
deleted. `Modlock.native` is the layout's own owner. `group()` makes another,
for changes released early, such as a pooled tooltip's on hover-out. Make a
group once and reuse it after each release. When a layout unloads, its
`onUnload` callbacks run first, then every owner it made is released.

Panorama keeps a panel's `visible` apart from its inline `visibility`.
Setting the style to `collapse` also turns `visible` off, clearing the style
applies only the game's own CSS and leaves `visible` as it was, and setting
`visible` writes the inline style. So `set(panel, { visible })` changes
`style.visibility`, and releasing `style.visibility` sets `visible = true`
before it writes the original. A panel whose original was no inline style is
then shown or hidden by the game's CSS, as a fresh game has it.

A layout never writes a native label's text. Once a script writes it, the game
stops filling the label, and nothing gives that back: not the saved text, the
token, nor `SetLocString` (measured in Deadlock). To show its own text, a
layout calls `relabel`. It hides the native label and creates a label of its
own right after it, with the native label's id and the classes given, and
returns that label. Calling it again on each sweep rewrites the same label, and
a native label that already reads the text is left alone. No owner relabels a
label `relabel` drew, which matters when a sweep runs in the frame of a
release, before the game deletes the old label. Release deletes the label and
shows the native one again:

```js
const tip = Modlock.native.group()
tip.relabel(tooltip.FindChildTraverse('ModName'), 'Snow Globe', ['ModName'])
```

`create` makes a panel inside a native panel, which the owner deletes on
release. `placement` puts it just before or after one of the parent's
children; only the created panel moves. `SetImage` and `SetPanelEvent` have
no getter, so a layout never calls them, `MoveChildBefore` or `MoveChildAfter`
on a native panel. It creates a panel and gives that panel the image or the
events:

```js
const cover = Modlock.native.create('Image', slot, 'Cover', { before: 'cooldown_mask' })
cover.SetImage('file://{images}/...')
```

Classes, dialog variables and children the game made are outside the API, and
a layout does not change them.

## Session commands

The relay stream carries `panorama.Frame`s (`panorama/stream.proto`): the
player's screen, `connect` with a loopback port, or `disconnect`.
`Relay.Send` writes a command on an open stream and reports whether one took
it. The renderer runs `connect 127.0.0.1:<port>` or `disconnect` as a console
command, and refuses and logs any other frame, including a port outside 1 to
65535. It never sends either on its own.

## Native tier definitions

Use a separate ability definition entry for tier bonuses. Base properties and
tier bonuses can target the same ability in adjacent entries:

```json
{
  "definitions": [
    {
      "file": "FILE_ABILITIES",
      "entry": "citadel_ability_hook",
      "properties": {
        "AbilityCastRange": 1181.1023622047244,
        "AbilityCooldown": 5
      }
    },
    {
      "file": "FILE_ABILITIES",
      "entry": "citadel_ability_hook",
      "upgrades": [
        { "tier": 2, "properties": { "AbilityCastRange": 236.2204724409449 } },
        { "tier": 3, "properties": { "AbilityCooldown": -0.5 } }
      ]
    }
  ]
}
```

This example replaces the existing second-tier range bonus with 6m and the
third-tier cooldown bonus with -0.5s. It leaves the first tier unchanged.
Values use native engine units and the existing row's operation. A value is
not necessarily the final ability statistic. The writer leaves native tier
costs unchanged; the tested Hook uses 1/2/5.

An entry containing `upgrades` accepts only `FILE_ABILITIES` and cannot also
contain `properties`, `fields` or `copy_fields`. Each requested tier must be
unique, exist in the native ability and contain existing named properties with
finite values. The writer changes both normal and Street Brawl numeric caches;
it does not add rows, change strings, or grant points.

A tier entry may include `"upgrade_order": [2, 3, 1]` to move existing property
vectors into that tier order. The list must contain every native tier exactly
once. Property edits in the same entry address the resulting tiers. Repeated
application starts from the original vectors. Native tier identities and masks
remain in their existing positions, preserving point costs and prerequisites.
Each property's parsed owner link follows its destination tier; native property
caches retain the same row addresses. The writer rejects unsupported parsed
layouts, unexpected owner links and inline property storage before writing.

All requested tier rows are resolved and checked before the first write.
Malformed requests leave that entry unchanged. Write or readback failures
trigger restoration of attempted writes, including a partially written target.
A restoration failure is reported explicitly; disappearing or inaccessible
native memory cannot be promised an infallible rollback. The original values
remain registered with the existing unload restoration owner.

This guarantee applies to the tier entry. Ordinary entries retain upstream's
application behavior, and the complete manifest is not a transaction.

## Maintaining the fork

Keep each extension separate from upstream changes. Before an upstream merge,
check whether upstream now supplies it; remove duplicates through a reviewed
change. Keep generated protocol outputs together with their source definitions.
Do not hand-edit generated bindings or copy mode-specific balance into runtime
code.

Run the native tier, modifier and input tests, the Panorama compiler tests, and
matching CLI/host builds after relevant changes. Repeat live tests after a game
update: fixture tests verify validation and restoration, while a live client
establishes that the current engine still uses the expected values.

Adopting native progression also requires mode changes: award points, preserve
ranks across pawn replacement, reset them deliberately between matches, prevent
spending on unwanted abilities, and give one system ownership of cooldowns.
Those changes are not part of this runtime branch.
