package session

import (
	"context"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/paralin/modlock/panorama"
	"github.com/paralin/modlock/src/wasm/ui"
)

// RelayAgent marks the game's own HTML panels in their User-Agent. Requiring
// it keeps the relay page, opened in an ordinary browser, from reading the
// player's screen. A Steam beta client names its
// branch after the marker, as "Source2 HTML [Steam Beta Update]/".
const RelayAgent = "Source2 HTML"

// FrameGap is the least time between two frames on one stream. The relay page
// acknowledges each frame before it reads the next, so a mod that changes its
// interface every server frame would otherwise queue frames in the socket
// faster than the game draws them; the stream sends the newest screen instead.
const FrameGap = 50 * time.Millisecond

// Keepalive is the longest the stream stays quiet: while the screen is
// unchanged it writes AliveLine this often. The renderer gives the game's HUD
// back once its titles stop for its lease, 30 s, so a quiet stream must still
// show it is delivering; sending the screen again would cost a board's
// several acknowledged title parts.
const Keepalive = 5 * time.Second

// AliveLine is the stream's keepalive. The relay page carries a line that is
// not a packed screen as a JSON line part, which the renderer acknowledges.
// The screen says whether the player is in the server, so the keepalive only
// says the stream delivers; it follows any change on the same stream.
const AliveLine = "alive\n"

// ScreenSource returns a player's screen and a channel that closes when it
// may have changed.
type ScreenSource func() (*ui.Screen, <-chan struct{})

// Relay serves one player's screen to their game on loopback. Deadlock loads
// only https pages into its HTML panels, so the game's renderer reads through
// a relay page from another origin: the page streams the screen from GET
// /panorama/stream, one binary panorama.Frame per line, packed by
// panorama.AppendPackedLine. A frame holds the screen, or a command the
// renderer runs in the game. The game presses buttons with a console command
// to the server, not through this page.
type Relay struct {
	// origin is the relay page's origin.
	origin string
	// screen is the player's screen.
	screen ScreenSource
	// commands hands each command to one open stream.
	commands chan command
	// keepalive is the longest the stream stays quiet, Keepalive but in tests.
	keepalive time.Duration
}

// command is a frame to send and a channel closed once a stream wrote it.
type command struct {
	frame *panorama.Frame
	sent  chan struct{}
}

// NewRelay returns a Relay serving screen to the relay page at origin.
func NewRelay(origin string, screen ScreenSource) *Relay {
	return &Relay{origin: origin, screen: screen, commands: make(chan command), keepalive: Keepalive}
}

// Send sends a command frame to the game on an open stream, and reports
// whether one wrote it before ctx ended.
func (r *Relay) Send(ctx context.Context, frame *panorama.Frame) bool {
	c := command{frame: frame, sent: make(chan struct{})}
	select {
	case r.commands <- c:
	case <-ctx.Done():
		return false
	}
	select {
	case <-c.sent:
		return true
	case <-ctx.Done():
		return false
	}
}

// ServeHTTP serves the screen stream to the relay page.
func (r *Relay) ServeHTTP(w http.ResponseWriter, request *http.Request) {
	if !r.admit(w, request) {
		return
	}
	switch {
	case request.Method == http.MethodGet && request.URL.Path == "/panorama/stream":
		r.stream(w, request)
	default:
		http.NotFound(w, request)
	}
}

// admit refuses requests from anything but the game's relay page or a
// same-origin page on loopback, and answers the relay's preflight. It reports
// whether the request needs more.
func (r *Relay) admit(w http.ResponseWriter, request *http.Request) bool {
	// Admit only loopback host names, which keeps DNS rebinding out, and
	// only the relay or a same-origin page.
	origin := request.Header.Get("Origin")
	relay := origin != "" && origin == r.origin && strings.Contains(request.UserAgent(), RelayAgent)
	if !Loopback(request) || !relay && !SameOrigin(request) {
		http.Error(w, "Forbidden", http.StatusForbidden)
		return false
	}

	// Answer the relay under CORS and Private Network Access.
	if relay {
		// The relay is a public https page reaching a private address, so the
		// browser asks first under CORS and Private Network Access.
		w.Header().Set("Access-Control-Allow-Origin", r.origin)
		w.Header().Set("Access-Control-Allow-Private-Network", "true")
		w.Header().Set("Vary", "Origin")
		if request.Method == http.MethodOptions {
			w.Header().Set("Access-Control-Allow-Methods", "GET")
			w.WriteHeader(http.StatusNoContent)
			return false
		}
	}
	w.Header().Set("Cache-Control", "no-store")
	return true
}

// stream writes the player's screen, then each changed screen and each
// command, and the keepalive after each quiet Keepalive, until the page goes
// away.
func (r *Relay) stream(w http.ResponseWriter, request *http.Request) {
	// Write packed lines of text, flushing each one.
	controller := http.NewResponseController(w)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	var line []byte
	sequence := uint64(1)
	write := func(frame *panorama.Frame) bool {
		data, err := frame.MarshalVT()
		if err != nil {
			return false
		}
		line = panorama.AppendPackedLine(line[:0], sequence, data)
		sequence++
		_, err = w.Write(line)
		return err == nil && controller.Flush() == nil
	}
	var sent *ui.Screen
	quiet := false
	for {
		// Send the screen as one packed line, unless it is the one sent, and
		// the keepalive after a quiet wait.
		screen, changed := r.screen()
		switch {
		case sent == nil || !screen.EqualVT(sent):
			if !write(&panorama.Frame{Body: &panorama.Frame_Screen{Screen: screen}}) {
				return
			}
			sent = screen
		case quiet:
			if _, err := w.Write([]byte(AliveLine)); err != nil || controller.Flush() != nil {
				return
			}
		}

		// Wait for a change, then let the page draw before the next frame;
		// a command goes at once.
		timer := time.NewTimer(r.keepalive)
		select {
		case <-changed:
			timer.Stop()
			quiet = false
		case c := <-r.commands:
			timer.Stop()
			if !write(c.frame) {
				return
			}
			close(c.sent)
			quiet = false
			continue
		case <-timer.C:
			quiet = true
			continue
		case <-request.Context().Done():
			timer.Stop()
			return
		}
		select {
		case <-time.After(FrameGap):
		case <-request.Context().Done():
			return
		}
	}
}

// Loopback reports whether request names a loopback host, which keeps DNS
// rebinding from reaching a server that listens on loopback.
func Loopback(request *http.Request) bool {
	host, _, err := net.SplitHostPort(request.Host)
	return err == nil && (host == "127.0.0.1" || strings.EqualFold(host, "localhost"))
}

// SameOrigin reports whether request comes from a page this server served, or
// from no page at all.
func SameOrigin(request *http.Request) bool {
	origin := request.Header.Get("Origin")
	return origin == "" || origin == "http://"+request.Host
}

var _ http.Handler = (*Relay)(nil)
