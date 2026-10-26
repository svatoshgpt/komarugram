// Package komaru talks to the KomaruGram plugin host from a Go plugin.
//
// A Go plugin is an .elyx archive whose metainfo says `runtime: go`. The
// host starts the plugin's binary for the current platform (or `go run .`
// when Go is installed) and exchanges one JSON object per line with it on
// stdin and stdout. Write logs to stderr; stdout belongs to the protocol.
//
//	func main() {
//		plugin := komaru.New()
//		plugin.Subscribe()
//		plugin.OnMessage(func(m komaru.Message) {
//			if m.Outgoing && m.Text == ".ping" {
//				plugin.SendMessage(m.Account, m.ChatID, "pong")
//			}
//		})
//		plugin.Run()
//	}
//
// Each Account carries the DC address and auth key, so a plugin can also
// open its own MTProto connection with gotd/td instead of asking the host.
package komaru

import (
	"bufio"
	"encoding/json"
	"os"
	"sync"
)

type Device struct {
	Model      string `json:"model"`
	System     string `json:"system"`
	App        string `json:"app"`
	Lang       string `json:"lang"`
	SystemLang string `json:"system_lang"`
}

type Account struct {
	Index   int    `json:"index"`
	UserID  int64  `json:"user_id"`
	DcID    int    `json:"dc_id"`
	IP      string `json:"ip"`
	Port    int    `json:"port"`
	AuthKey string `json:"auth_key"` // 256 bytes, hex
	Test    bool   `json:"test"`
	APIID   int    `json:"api_id"`
	APIHash string `json:"api_hash"`
	Device  Device `json:"device"`
}

type Message struct {
	Account   int    `json:"account"`
	ChatID    int64  `json:"chat_id"`
	ChatType  string `json:"chat_type"`
	SenderID  int64  `json:"sender_id"`
	MessageID int    `json:"message_id"`
	Text      string `json:"text"`
	Outgoing  bool   `json:"outgoing"`
}

// Outgoing is what a send hook returns: Keep, Modify with new text, or
// Cancel. Declare `hooks: [send_message]` in the metainfo to get them.
type Outgoing struct {
	Action string
	Text   string
}

type Plugin struct {
	Accounts []Account
	Settings map[string]any
	Meta     map[string]any

	mu        sync.Mutex
	out       *json.Encoder
	nextID    int
	onMessage func(Message)
	onSend    func(account int, peer int64, text string) Outgoing
	onSetting func(key string, value any)
	onStop    func()
}

func New() *Plugin {
	return &Plugin{out: json.NewEncoder(os.Stdout), Settings: map[string]any{}}
}

func (p *Plugin) send(message map[string]any) {
	p.mu.Lock()
	defer p.mu.Unlock()
	_ = p.out.Encode(message)
}

func (p *Plugin) OnMessage(callback func(Message))                  { p.onMessage = callback }
func (p *Plugin) OnSetting(callback func(key string, value any))    { p.onSetting = callback }
func (p *Plugin) OnStop(callback func())                            { p.onStop = callback }
func (p *Plugin) OnSend(cb func(int, int64, string) Outgoing)       { p.onSend = cb }
func (p *Plugin) Subscribe()                                        { p.send(map[string]any{"call": "subscribe", "what": "updates"}) }
func (p *Plugin) Log(text string)                                   { p.send(map[string]any{"call": "log", "text": text}) }
func (p *Plugin) Toast(text string)                                 { p.send(map[string]any{"call": "toast", "text": text}) }
func (p *Plugin) SetSetting(key string, value any)                  { p.send(map[string]any{"call": "set_setting", "key": key, "value": value}) }

// SendMessage sends text through the host's Telethon client. parseMode is
// "", "md" or "html".
func (p *Plugin) SendMessage(account int, peer int64, text string, parseMode ...string) {
	p.mu.Lock()
	p.nextID++
	id := p.nextID
	p.mu.Unlock()
	message := map[string]any{"call": "send_message", "id": id, "account": account, "peer": peer, "text": text}
	if len(parseMode) > 0 {
		message["parse_mode"] = parseMode[0]
	}
	p.send(message)
}

// Run reads host events until the host stops the plugin.
func (p *Plugin) Run() {
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 1024*1024), 16*1024*1024)
	for scanner.Scan() {
		var event map[string]json.RawMessage
		if json.Unmarshal(scanner.Bytes(), &event) != nil {
			continue
		}
		var name string
		_ = json.Unmarshal(event["event"], &name)
		switch name {
		case "init":
			_ = json.Unmarshal(event["accounts"], &p.Accounts)
			_ = json.Unmarshal(event["settings"], &p.Settings)
			_ = json.Unmarshal(event["plugin"], &p.Meta)
		case "accounts":
			_ = json.Unmarshal(event["accounts"], &p.Accounts)
		case "update":
			var message Message
			if json.Unmarshal(scanner.Bytes(), &message) == nil && p.onMessage != nil {
				go p.onMessage(message)
			}
		case "setting":
			var key string
			var value any
			_ = json.Unmarshal(event["key"], &key)
			_ = json.Unmarshal(event["value"], &value)
			p.Settings[key] = value
			if p.onSetting != nil {
				p.onSetting(key, value)
			}
		case "send_message":
			var request struct {
				ID      int    `json:"id"`
				Account int    `json:"account"`
				Peer    int64  `json:"peer"`
				Text    string `json:"text"`
			}
			_ = json.Unmarshal(scanner.Bytes(), &request)
			reply := Outgoing{Action: "keep"}
			if p.onSend != nil {
				reply = p.onSend(request.Account, request.Peer, request.Text)
			}
			p.send(map[string]any{"reply": request.ID, "action": reply.Action, "text": reply.Text})
		case "stop":
			if p.onStop != nil {
				p.onStop()
			}
			return
		}
	}
}
