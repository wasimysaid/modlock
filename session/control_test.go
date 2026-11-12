package session

import (
	"encoding/binary"
	"net"
	"testing"

	"github.com/paralin/modlock/src/host_app/control"
	"github.com/paralin/modlock/src/wasm/ui"
)

// TestControlOwnsUiGeneration keeps mod-authored lifetime metadata out of
// native events before they reach any screen owner or peer feed.
func TestControlOwnsUiGeneration(t *testing.T) {
	// Start the real native control boundary and record the event it forwards.
	link, err := NewControl()
	if err != nil {
		t.Fatal(err)
	}
	defer link.Close()
	var received *control.HostEvent
	done := make(chan error, 1)
	go func() {
		done <- link.Run(t.Context(), func(event *control.HostEvent) { received = event })
	}()

	// A mod supplies a generation in an otherwise valid UI update.
	event := &control.HostEvent{Body: &control.HostEvent_Ui{Ui: &control.UiChanged{
		Mod: "arena", Change: &ui.Change{Reset_: true, Generation: 1234},
	}}}
	data, err := event.MarshalVT()
	if err != nil {
		t.Fatal(err)
	}
	conn, err := net.Dial("tcp", link.Address())
	if err != nil {
		t.Fatal(err)
	}
	frame := binary.LittleEndian.AppendUint32(nil, uint32(len(data)))
	if _, err := conn.Write(append(frame, data...)); err != nil {
		t.Fatal(err)
	}
	conn.Close()

	// The native event keeps its reset, while its metadata is host-owned.
	if err := <-done; err != nil {
		t.Fatal(err)
	}
	change := received.GetUi().GetChange()
	if !change.GetReset_() || change.GetGeneration() != 0 {
		t.Fatalf("the mod selected the UI lifetime: %v", received)
	}
}

// TestControlSkipsConnectionsThatAreNotTheHost keeps the link for the host
// when another program on the PC reaches the port first: one that closes, and
// one that sends something that is no event.
func TestControlSkipsConnectionsThatAreNotTheHost(t *testing.T) {
	link, err := NewControl()
	if err != nil {
		t.Fatal(err)
	}
	defer link.Close()
	if err := link.Reload(t.Context(), "arena"); err != nil {
		t.Fatal(err)
	}
	var received *control.HostEvent
	done := make(chan error, 1)
	go func() {
		done <- link.Run(t.Context(), func(event *control.HostEvent) { received = event })
	}()

	// Two strangers come and go before the host.
	for _, sent := range []string{"", "HUAD not a frame"} {
		stranger, err := net.Dial("tcp", link.Address())
		if err != nil {
			t.Fatal(err)
		}
		if _, err := stranger.Write([]byte(sent)); err != nil {
			t.Fatal(err)
		}
		stranger.Close()
	}

	// The host's first event arrives, and the waiting request reaches it.
	event := &control.HostEvent{Body: &control.HostEvent_Ready{Ready: &control.ServerReady{Map: "arena"}}}
	data, err := event.MarshalVT()
	if err != nil {
		t.Fatal(err)
	}
	host, err := net.Dial("tcp", link.Address())
	if err != nil {
		t.Fatal(err)
	}
	if _, err := host.Write(append(binary.LittleEndian.AppendUint32(nil, uint32(len(data))), data...)); err != nil {
		t.Fatal(err)
	}
	request := &control.ControlRequest{}
	if err := readMessage(host, request); err != nil {
		t.Fatal(err)
	}
	if request.GetReload().GetPath() != "arena" {
		t.Fatalf("the host got %v, not the waiting reload", request)
	}
	host.Close()
	if err := <-done; err != nil {
		t.Fatal(err)
	}
	if received.GetReady().GetMap() != "arena" {
		t.Fatalf("the host's event was lost: %v", received)
	}
}
