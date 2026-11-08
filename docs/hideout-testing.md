# Hideout native UI and ability test findings

These findings come from local Deadlock tests on 2026-11-08. They describe the
observed behavior, not a completed gameplay migration.

## Test versions

The initial tests used upstream `29ae444` with the Bebop modifier, input and
Panorama image patches. The numeric tier test added an experimental definition
writer extension. The maintained branch also contains upstream `d54df29`.
The gameplay installation remained pinned to its original runtime during this
fork review.

## Native UI

Native Hook controls display the player's binding and actual cooldown. Native
item cards and full item tooltips can display custom artwork and descriptions.
The stock shop still owns its normal catalog and purchasing behavior. A tested
adapter wrapped native card visuals in a mode button and sent the purchase to
the mode's authoritative wallet and ownership handler.

Client tooltip metadata and server definitions are separate. Server definition
changes alone did not update the stock range, cooldown, or upgrade descriptions.
A Panorama adapter changed the existing native tooltip using authoritative rank
state. It displayed 36m and 4.5s after both corresponding test upgrades.

Deadlock reuses tooltip panels between abilities. A text adapter must restore
the native dialog-variable values when the tooltip switches to another ability.
The Hook, Uppercut, Hook sequence passed after this restoration was added.
The research adapter uses polling and an English ability name. Those choices
are not a production localization or lifecycle design.

## Native point spending

The tests awarded points with `adjustAbilityPoints` and spent them through the
real native buttons and the player's upgrade-key plus Hook-key sequence.
They did not simulate purchases by writing upgrade bits.

| Test | Server observation |
| --- | --- |
| Spend first tier | Points 1 to 0; upgrade mask 1 to 3 |
| Buy second tier with one point | No point or rank change |
| Spend second tier | Points 2 to 0; mask 3 to 7 |
| Spend third tier | Points 8 to 3; mask 7 to 15 |
| Repeat input at maximum rank | No further charge or rank change |

Point costs remain the native 1, 2 and 5. Custom costs are outside the requested
scope. Points, base ability unlocks and purchased ranks are distinct state.

The game mode cleared points and reset ranks in its recurring rules and respawn
handler. Disabling those policies in the test copy let the native progression
persist through death and reconnect to the same server. The reconnect retained
the same pawn. Pawn replacement, server restart and match reset were not proven
by that test. The mode needs authoritative progression records for those paths.
Kill-earned point rewards have not been implemented by this investigation.

## Numeric tier effects

The experiment changed existing parsed native upgrade bonuses. It used schema
fields to locate the tier and property vectors, then changed the normal and
Street Brawl numeric caches. It preserved the engine's vectors and strings.

The test profile used these values:

| Tier | Properties |
| --- | --- |
| 1 | `BulletAmp = 0`, `BulletAmpDuration = 0` |
| 2 | `AbilityCastRange = 236.2204724409449` engine units, equivalent to 6m |
| 3 | `AbilityCooldown = -0.5` seconds |

The empty first tier was a test fixture, not a recommended progression tree.

| Rank | Native result after an executed throw |
| --- | --- |
| Base | 5.0s cooldown; maximum travel time 0.4144218862s |
| Stock second tier, earlier control | Maximum travel time 0.8288437724s |
| Custom second tier | Maximum travel time 0.4973062575s |
| Custom third tier | 4.5s cooldown; travel limit unchanged from tier 2 |

With the unchanged 2850-unit/s outgoing speed, these travel limits correspond
to 30m, 60m and 36m. The custom range replaced the original bonus rather than
stacking with it. In a clean session, the upgraded Hook caught a bot about 34m
away. Base Hook missed with the same placement and aim. The upgraded Hook missed
at about 40m. This is not an exact collision-boundary measurement.

The test disabled scripted cooldown enforcement. Otherwise the mode would
continue replacing the engine's timer with five seconds. Production must have
one owner for the effective cooldown.

No amplification modifier was observed after a successful hit with the first
tier configured to zero. The original-bonus positive control and a weapon-damage
comparison were not run. That absence is not complete validation of bonus removal.

These tests establish numeric edits to existing bonus rows. They do not establish
adding rows, moving a bonus between tiers, or replacing a tier with an unrelated
scripted mechanic. Server-side changes also do not establish correct client
prediction. The unadapted tooltip still showed the stock values during the test.

## Background control

The client ran on Gamescope's headless display at 1280 by 720. The existing Proton
build used its X11 driver inside that display. Keyboard and mouse input targeted
Gamescope's private Xwayland display. `gamescopectl screenshot` captured the
rendered client. No desktop-wide input injection was needed.

Start the development server before the client so the client mounts the current
content. A server restart alone does not establish that an already-running
client loaded a replacement package.

## Remaining integration checks

- Preserve paid ranks and points across pawn replacement and match reset.
- Keep tooltip values synchronized with the actual native rank and cooldown owner.
- Verify bonus removal with a positive control before enabling a combat mode.
- Test packaging and joining from a clean second installation.

Snow Globe flight and detonation HUD tests are deferred. A throw-only design is
an option, not an implemented change.

## Maintained fork verification

The hardened writer was rebuilt with upstream `d54df29` and the retained Bebop
extensions on 2026-11-08. The fresh Linux CLI, embedded scripting library and
renderer, complete Windows host build, Go package tests and TypeScript checks
passed. The full build also compiled the Go example after fixing a nested-enum
constant naming defect in the SDK generator.

All 33 selected native tests passed under Proton: 15 tier-definition tests,
6 modifier tests, 7 queued-input tests, 1 input-hook test and 4 console-variable
tests. Tier fixtures cover rejection before native lookup, malformed schema and
vectors, bounded names, overlapping storage, finite values, partial writes,
readback failure, rollback failure reporting, and unload restoration after
repeated applications and world changes.

A fresh off-screen Deadlock session loaded six definition entries with zero
reported problems. Real native key purchases reproduced the 1/2/5 costs and
upgrade masks. Actual throws reproduced these server measurements:

| Rank | Cooldown duration | Maximum travel time |
| --- | --- | --- |
| Base | 5.0s | 0.4144218862s |
| Tier 2 | 5.0s | 0.4973062575s |
| Tier 3 | 4.5s | 0.4973062575s |

The upgraded Hook caught the stationary bot at about 34m. Resetting to the base
rank and repeating the same positions and aim missed, with the bot remaining
alive. This repeated the range behavior on the maintained writer.

Buying tier 3 during a running cooldown also changed that timer. Its endpoint
moved from 67.015625 to 66.8249969 seconds, and its start value changed too.
The next actual throw separately confirmed the 4.5-second duration. A probe
that treats every cooldown-start change as a cast therefore reports a false
cast on that upgrade. Gameplay should use executed-ability observations for
cast attribution and decide explicitly whether to accept native cooldown
adjustments when buying ranks. A future-throws-only rule is not supplied by
the native upgrade path.

These checks establish the tier layout and behavior on this installed game.
They do not establish compatibility with future game builds, client prediction,
or delivery to a clean second installation. The installed Bebop runtime and its
version lock were not replaced by this verification build.
