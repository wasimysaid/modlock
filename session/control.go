// Package session runs a play session: modlock-host with the mods, a Deadlock
// client that joins it, and the control link that reports the mods' progress
// and reloads them.
package session

import (
	"bufio"
	"context"
	"encoding/binary"
	"io"
	"net"

	"github.com/paralin/modlock/src/host_app/control"
	"github.com/paralin/modlock/src/wasm/players"
	"github.com/pkg/errors"
)

// maxMessage bounds one framed message from the host.
const maxMessage = 10_000_000

// Control is the controller's end of the control link. It listens on a
// loopback port and modlock-host dials it with --control; then the host
// streams its events and takes reloads and the players' roles, each message
// framed by its length as control.proto describes.
type Control struct {
	// listener accepts the host's connection.
	listener net.Listener
	// requests carries requests to the host once it connects.
	requests chan *control.ControlRequest
}

// NewControl listens on a free loopback port.
func NewControl() (*Control, error) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return nil, errors.Wrap(err, "listen for the host")
	}
	return &Control{listener: listener, requests: make(chan *control.ControlRequest, 16)}, nil
}

// Address returns the address to pass to modlock-host --control.
func (c *Control) Address() string {
	return c.listener.Addr().String()
}

// Reload asks the host to replace the running mod named by the build at path.
// Requests made before the host connects wait for it.
func (c *Control) Reload(ctx context.Context, path string) error {
	return c.request(ctx, &control.ControlRequest{
		Body: &control.ControlRequest_Reload{Reload: &control.ReloadRequest{Path: path}},
	})
}

// SetService installs a service answer and restarts its mod with that answer.
func (c *Control) SetService(ctx context.Context, reply *control.ServiceReply) error {
	return c.request(ctx, &control.ControlRequest{Body: &control.ControlRequest_Service{Service: reply}})
}

// SetRoles replaces the players' roles, which the mods read on each
// connection. Requests made before the host connects wait for it.
func (c *Control) SetRoles(ctx context.Context, roles *players.RolesTable) error {
	return c.request(ctx, &control.ControlRequest{
		Body: &control.ControlRequest_Roles{Roles: roles},
	})
}

// request queues request for the call.
func (c *Control) request(ctx context.Context, request *control.ControlRequest) error {
	select {
	case c.requests <- request:
		return nil
	case <-ctx.Done():
		return context.Canceled
	}
}

// Run accepts the host's connection and passes each host event to handle
// until the host disconnects or ctx ends. It returns nil when the host
// disconnects. The port is open to every program on the PC, so a connection
// that closes or sends anything but an event first is not the host: Run drops
// it and waits for the next, and no request goes to it.
func (c *Control) Run(ctx context.Context, handle func(*control.HostEvent)) error {
	// Accept connections until one is the host; closing the listener ends
	// the wait with ctx.
	stop := context.AfterFunc(ctx, func() { _ = c.listener.Close() })
	defer stop()
	for {
		conn, err := c.listener.Accept()
		if err != nil {
			if ctx.Err() != nil {
				return context.Canceled
			}
			return errors.Wrap(err, "accept the host")
		}
		hosted, err := c.serve(ctx, conn, handle)
		if hosted || err != nil {
			return err
		}
	}
}

// serve passes conn's events to handle until it disconnects, and reports
// whether conn was the host: whether its first message was an event.
func (c *Control) serve(ctx context.Context, conn net.Conn, handle func(*control.HostEvent)) (bool, error) {
	defer conn.Close()
	stopRead := context.AfterFunc(ctx, func() { _ = conn.Close() })
	defer stopRead()
	sendCtx, cancel := context.WithCancel(ctx)
	defer cancel()

	// Pass events until the host disconnects.
	reader := bufio.NewReader(conn)
	hosted := false
	for {
		event := &control.HostEvent{}
		if err := readMessage(reader, event); err != nil {
			if ctx.Err() != nil {
				return hosted, context.Canceled
			}
			return hosted, nil
		}

		// Forward requests once the connection proves to be the host.
		if !hosted {
			hosted = true
			go c.send(sendCtx, conn)
		}

		// UI lifetimes belong to the screen owner, not the mod that filled
		// the native request. Peer snapshots enter through another path.
		if change := event.GetUi().GetChange(); change != nil {
			change.Generation = 0
		}
		handle(event)
	}
}

// send writes requests to conn until ctx ends or a write fails.
func (c *Control) send(ctx context.Context, conn net.Conn) {
	for {
		select {
		case <-ctx.Done():
			return
		case request := <-c.requests:
			data, err := request.MarshalVT()
			if err != nil {
				continue
			}
			frame := binary.LittleEndian.AppendUint32(make([]byte, 0, 4+len(data)), uint32(len(data)))
			if _, err := conn.Write(append(frame, data...)); err != nil {
				return
			}
		}
	}
}

// readMessage reads one framed message into message.
func readMessage(reader io.Reader, message interface{ UnmarshalVT([]byte) error }) error {
	// Bound the payload before allocating its frame.
	var header [4]byte
	if _, err := io.ReadFull(reader, header[:]); err != nil {
		return err
	}
	size := binary.LittleEndian.Uint32(header[:])
	if size > maxMessage {
		return errors.Errorf("the host sent a %d byte message", size)
	}

	// Decode the complete frame, without consuming the following message.
	data := make([]byte, size)
	if _, err := io.ReadFull(reader, data); err != nil {
		return err
	}
	return message.UnmarshalVT(data)
}

// Close stops listening for the host.
func (c *Control) Close() error {
	return c.listener.Close()
}
