// The bridge reads the local player's screen from the controller on loopback.
// Deadlock loads only https pages into HTML panels, so the panel loads a relay
// page from an https origin that reaches the controller and streams each
// screen into its title in acknowledged parts. The same stream carries the
// two console commands the session may send the player's game: connect to a
// server on loopback, and disconnect.

import type { Screen } from '../../src/wasm/ui/ui.pb.js'
import { Frame } from '../stream.pb.js'
import { unpackBytes } from './packed.js'

/**
 * titlePrefix marks a part of a packed screen in the state page's title:
 * "<prefix><id>:<offset>:<total>:<sequence>:<chunk>", with the offset and
 * total counted in characters.
 */
const titlePrefix = 'modlock:frame:'

/** loadDeadline is how long a page may take to load before it is loaded again. */
const loadDeadline = 10

/** Bridge connects the renderer to the controller through a relay page. */
export class Bridge {
  /** parts holds the parts of the screen arriving now. */
  private parts: { id: number; total: number; text: string } | null = null
  /** lastTitle is the last part read; navigating to a fragment repeats it. */
  private lastTitle = ''
  /** loading is the deadline of the state page's load, if it is loading. */
  private loading: number | undefined
  /** acked flips with each acknowledgment, between the fragments #0 and #1. */
  private acked = false

  constructor(
    private readonly statePanel: Panel,
    private readonly stateURL: string,
    private readonly show: (screen: Screen | null) => void,
    private readonly run: (command: string) => void,
  ) {
    // Registering the navigation event lets the panel navigate.
    $.RegisterEventHandler('HTMLStartRequest', statePanel, () => false)
    $.RegisterEventHandler('HTMLTitle', statePanel, (_: never, title: string) =>
      this.read(title),
    )
    this.load()
  }

  /** load loads the state page, and loads it again if no title arrives in time. */
  private load(): void {
    this.statePanel.SetURL(this.stateURL)
    this.loading = $.Schedule(loadDeadline, () => {
      this.loading = undefined
      this.load()
    })
  }

  /** read takes one state title: a part of a frame, or the page's status. */
  private read(title: string): boolean {
    // A title means the page loaded; cancel its load deadline.
    if (this.loading !== undefined) {
      $.CancelScheduled(this.loading)
      this.loading = undefined
    }

    // A status title means the stream is not delivering; drop the screen
    // when it ended.
    if (!title.startsWith(titlePrefix)) {
      this.parts = null
      this.lastTitle = ''
      if (title === '') {
        this.show(null)
      }
      return false
    }

    // Skip a title already read.
    if (title === this.lastTitle) {
      return false
    }
    this.lastTitle = title

    // Join the part to the screen it belongs to, and acknowledge it. The
    // relay waits for the fragment to change; two fixed fragments keep the
    // panel's history from growing with each part.
    let frame: Frame
    try {
      const fields = title.slice(titlePrefix.length).split(':', 4)
      const [id, offset, total] = fields.map(Number)
      const chunk = title.slice(
        titlePrefix.length + fields.join(':').length + 1,
      )
      if (fields.length !== 4 || !id || !total) {
        throw new Error('a part of the screen is malformed')
      }
      if (offset === 0) {
        this.parts = { id, total, text: '' }
      }
      const parts = this.parts
      if (!parts || parts.id !== id || parts.text.length !== offset) {
        throw new Error('a part of the screen is missing')
      }
      parts.text += chunk
      this.acked = !this.acked
      this.statePanel.SetURL(`${this.stateURL}#${this.acked ? 1 : 0}`)
      if (parts.text.length < parts.total) {
        return false
      }
      this.parts = null
      const bytes = unpackBytes(parts.text)
      if (!bytes) {
        throw new Error('the screen is not packed bytes')
      }
      frame = Frame.fromBinary(bytes)
    } catch (error) {
      // The page times out the unacknowledged part and sends the screen again.
      $.Msg('[Modlock] could not read the screen: ', String(error))
      this.parts = null
      return false
    }

    // Dispatch the frame.
    const body = frame.body
    if (body?.case === 'screen') {
      try {
        this.show(body.value)
      } catch (error) {
        $.Msg('[Modlock] could not draw the screen: ', String(error))
      }
    } else if (body?.case === 'disconnect') {
      this.run('disconnect')
    } else if (
      body?.case === 'connect' &&
      body.value >= 1 &&
      body.value <= 65535
    ) {
      this.run(`connect 127.0.0.1:${body.value}`)
    } else {
      $.Msg(
        '[Modlock] refused a command: ',
        JSON.stringify(Frame.toJson(frame)),
      )
    }
    return false
  }
}
