package serial

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"math"
	"math/rand"
	"runtime"
	"strings"
	"sync"
	"time"

	"go.bug.st/serial"
	"go.uber.org/zap"
)

// Config holds serial port settings.
type Config struct {
	PortPath string
	BaudRate int
	Simulate bool
}

// Manager handles serial port lifecycle, reading status and writing commands.
type Manager struct {
	cfg       Config
	logger    *zap.Logger
	statusCh  chan json.RawMessage
	commandCh chan json.RawMessage
	port      serial.Port
	mu        sync.Mutex
	connected bool

	// Sim ELS state (protected by mu)
	simPitch       float64
	simDir         string
	simRetractMode string
	simPass        int
	simEngaged     bool
}

// NewManager creates a new serial manager.
func NewManager(cfg Config, logger *zap.Logger) *Manager {
	return &Manager{
		cfg:            cfg,
		logger:         logger.Named("serial"),
		statusCh:       make(chan json.RawMessage, 64),
		commandCh:      make(chan json.RawMessage, 32),
		simPitch:       1.5,
		simDir:         "rh",
		simRetractMode: "rapid",
	}
}

// StatusCh returns the channel that emits throttled status and ACK messages.
func (m *Manager) StatusCh() <-chan json.RawMessage {
	return m.statusCh
}

// CommandCh returns the channel to send commands to the serial port.
func (m *Manager) CommandCh() chan<- json.RawMessage {
	return m.commandCh
}

// Send writes a command to the serial port (or routes to sim handler).
func (m *Manager) Send(cmd json.RawMessage) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.cfg.Simulate {
		select {
		case m.commandCh <- cmd:
		default:
		}
		return
	}
	if m.port == nil {
		return
	}
	line := append(cmd, '\n')
	if _, err := m.port.Write(line); err != nil {
		m.logger.Warn("Serial write error", zap.Error(err))
	}
}

// Run starts the serial manager loop. Call in a goroutine.
func (m *Manager) Run(ctx context.Context) {
	if m.cfg.Simulate {
		m.runSimulated(ctx)
		return
	}

	for {
		select {
		case <-ctx.Done():
			return
		default:
		}

		portPath := m.cfg.PortPath
		if portPath == "" {
			portPath = m.autoDetect()
		}
		if portPath == "" {
			m.logger.Debug("No serial port found, retrying in 2s")
			select {
			case <-time.After(2 * time.Second):
				continue
			case <-ctx.Done():
				return
			}
		}

		if err := m.openAndRun(ctx, portPath); err != nil {
			m.logger.Warn("Serial session ended", zap.Error(err))
		}

		m.sendMeta(false, "")

		select {
		case <-time.After(1 * time.Second):
		case <-ctx.Done():
			return
		}
	}
}

func (m *Manager) autoDetect() string {
	ports, err := serial.GetPortsList()
	if err != nil {
		m.logger.Debug("Port enumeration failed", zap.Error(err))
		return ""
	}

	for _, p := range ports {
		switch runtime.GOOS {
		case "darwin":
			if strings.Contains(p, "tty.usbmodem") {
				return p
			}
		case "linux":
			if strings.HasPrefix(p, "/dev/ttyACM") {
				return p
			}
		}
	}
	return ""
}

func (m *Manager) openAndRun(ctx context.Context, portPath string) error {
	mode := &serial.Mode{
		BaudRate: m.cfg.BaudRate,
		DataBits: 8,
		Parity:   serial.NoParity,
		StopBits: serial.OneStopBit,
	}

	p, err := serial.Open(portPath, mode)
	if err != nil {
		return fmt.Errorf("open %s: %w", portPath, err)
	}

	m.mu.Lock()
	m.port = p
	m.connected = true
	m.mu.Unlock()

	m.logger.Info("Serial port opened", zap.String("port", portPath))
	m.sendMeta(true, portPath)

	defer func() {
		m.mu.Lock()
		m.port = nil
		m.connected = false
		m.mu.Unlock()
		p.Close()
		m.logger.Info("Serial port closed", zap.String("port", portPath))
	}()

	// Raw status channel (before throttle)
	rawCh := make(chan json.RawMessage, 64)

	// Reader goroutine
	readerDone := make(chan error, 1)
	go func() {
		scanner := bufio.NewScanner(p)
		for scanner.Scan() {
			line := scanner.Bytes()
			if len(line) == 0 {
				continue
			}
			if !json.Valid(line) {
				continue
			}

			msg := make(json.RawMessage, len(line))
			copy(msg, line)

			// ACKs go directly (no throttle)
			if isACK(line) {
				select {
				case m.statusCh <- msg:
				default:
				}
			} else {
				select {
				case rawCh <- msg:
				default:
					// Drop oldest if full
					select {
					case <-rawCh:
					default:
					}
					rawCh <- msg
				}
			}
		}
		readerDone <- scanner.Err()
	}()

	// Writer goroutine
	writerDone := make(chan error, 1)
	go func() {
		for {
			select {
			case cmd := <-m.commandCh:
				m.mu.Lock()
				if m.port != nil {
					line := append(cmd, '\n')
					_, err := m.port.Write(line)
					if err != nil {
						m.mu.Unlock()
						writerDone <- err
						return
					}
				}
				m.mu.Unlock()
			case <-ctx.Done():
				writerDone <- nil
				return
			}
		}
	}()

	// Throttle loop: 50Hz → 20Hz
	throttleDone := make(chan struct{})
	go func() {
		defer close(throttleDone)
		ticker := time.NewTicker(50 * time.Millisecond)
		defer ticker.Stop()
		var latest json.RawMessage
		for {
			select {
			case msg := <-rawCh:
				latest = msg
			case <-ticker.C:
				if latest != nil {
					select {
					case m.statusCh <- latest:
					default:
					}
					latest = nil
				}
			case <-ctx.Done():
				return
			}
		}
	}()

	// Wait for reader or writer to finish
	select {
	case err := <-readerDone:
		return fmt.Errorf("reader: %w", err)
	case err := <-writerDone:
		return fmt.Errorf("writer: %w", err)
	case <-ctx.Done():
		return nil
	}
}

func (m *Manager) sendMeta(connected bool, port string) {
	msg := map[string]interface{}{
		"meta":   "connected",
		"serial": connected,
		"sim":    m.cfg.Simulate,
		"port":   port,
	}
	data, _ := json.Marshal(msg)
	select {
	case m.statusCh <- json.RawMessage(data):
	default:
	}
}

func isACK(data []byte) bool {
	// Quick check: ACK messages contain "ack" key
	return len(data) > 5 && strings.Contains(string(data), `"ack"`)
}

// --- Simulated mode ---

func (m *Manager) runSimulated(ctx context.Context) {
	m.logger.Info("Simulated serial mode active")
	m.sendMeta(true, "simulated")

	rawCh := make(chan json.RawMessage, 64)

	// Status generator at ~50Hz
	go func() {
		ticker := time.NewTicker(20 * time.Millisecond)
		defer ticker.Stop()
		t := 0.0
		passTimer := 0.0
		for {
			select {
			case <-ticker.C:
				t += 0.02

				m.mu.Lock()
				engaged := m.simEngaged
				pitch := m.simPitch
				dir := m.simDir
				retract := m.simRetractMode
				pass := m.simPass
				m.mu.Unlock()

				state := "idle"
				if engaged {
					state = "threading"
					passTimer += 0.02
					if passTimer >= 5.0 {
						passTimer = 0
						m.mu.Lock()
						m.simPass++
						m.mu.Unlock()
						pass = m.simPass
					}
				} else {
					passTimer = 0
					if math.Mod(t, 30.0) > 28.0 {
						state = "alarm"
					}
				}

				status := map[string]interface{}{
					"pos": map[string]interface{}{
						"x": 10.0 * math.Sin(t*0.6),
						"z": -25.0 + 25.0*math.Cos(t*0.3),
					},
					"rpm":   math.Max(0, 800.0+200.0*math.Sin(t*0.1)+50.0*rand.Float64()),
					"state": state,
					"fh":    false,
				}
				if engaged {
					status["pitch"] = pitch
					status["dir"] = dir
					status["pass"] = pass
					status["retract"] = retract
				}
				data, _ := json.Marshal(status)
				select {
				case rawCh <- json.RawMessage(data):
				default:
				}
			case <-ctx.Done():
				return
			}
		}
	}()

	// Throttle to 20Hz
	go func() {
		ticker := time.NewTicker(50 * time.Millisecond)
		defer ticker.Stop()
		var latest json.RawMessage
		for {
			select {
			case msg := <-rawCh:
				latest = msg
			case <-ticker.C:
				if latest != nil {
					select {
					case m.statusCh <- latest:
					default:
					}
					latest = nil
				}
			case <-ctx.Done():
				return
			}
		}
	}()

	// Command handler (simulated ACKs)
	for {
		select {
		case cmd := <-m.commandCh:
			m.handleSimCommand(cmd)
		case <-ctx.Done():
			return
		}
	}
}

func (m *Manager) handleSimCommand(cmd json.RawMessage) {
	var parsed struct {
		Cmd string `json:"cmd"`
	}
	if err := json.Unmarshal(cmd, &parsed); err != nil {
		return
	}

	var ack json.RawMessage
	switch parsed.Cmd {
	case "zero", "preset", "config_save", "config_set":
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": parsed.Cmd,
			"ok":  true,
		})
	case "config_get":
		var full struct {
			Key string `json:"key"`
		}
		json.Unmarshal(cmd, &full)
		val := m.simConfigValue(full.Key)
		ack, _ = json.Marshal(map[string]interface{}{
			"ack":   "config_get",
			"ok":    true,
			"key":   full.Key,
			"value": val,
		})
	case "config_list":
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "config_list",
			"ok":  true,
			"params": map[string]interface{}{
				"spindle_ppr":            1000,
				"spindle_quadrature":     4,
				"spindle_counts_per_rev": 4000,
				"spindle_max_rpm":        3500,
				"z_scale_resolution_mm":  0.005,
				"z_travel_min_mm":        -300.0,
				"z_travel_max_mm":        0.0,
				"z_leadscrew_pitch_mm":   6.0,
				"z_steps_per_rev":        1000,
				"z_belt_ratio":           1.0,
				"z_steps_per_mm":         166.667,
				"z_max_speed_mm_s":       50.0,
				"z_accel_mm_s2":          100.0,
				"z_backlash_mm":          0.0,
				"x_scale_resolution_mm":  0.005,
				"x_is_diameter":          true,
				"x_travel_min_mm":        -75.0,
				"x_travel_max_mm":        0.0,
				"x_steps_per_rev":        1000,
				"x_leadscrew_pitch_mm":   3.0,
				"x_belt_ratio":           1.0,
				"x_steps_per_mm":         333.333,
				"thread_retract_mode":    0,
				"thread_retract_x_mm":    1.0,
				"thread_compound_angle":  29.0,
			},
		})
	case "set_pitch":
		var full struct {
			Pitch float64 `json:"pitch"`
			Dir   string  `json:"dir"`
		}
		json.Unmarshal(cmd, &full)
		m.mu.Lock()
		if full.Pitch > 0 {
			m.simPitch = full.Pitch
		}
		if full.Dir == "rh" || full.Dir == "lh" {
			m.simDir = full.Dir
		}
		m.mu.Unlock()
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "set_pitch",
			"ok":  true,
		})
	case "set_retract":
		var full struct {
			Mode string `json:"mode"`
		}
		json.Unmarshal(cmd, &full)
		m.mu.Lock()
		if full.Mode == "rapid" || full.Mode == "spring" {
			m.simRetractMode = full.Mode
		}
		m.mu.Unlock()
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "set_retract",
			"ok":  true,
		})
	case "engage":
		m.mu.Lock()
		m.simEngaged = true
		m.simPass = 1
		m.mu.Unlock()
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "engage",
			"ok":  true,
		})
	case "disengage":
		m.mu.Lock()
		m.simEngaged = false
		m.simPass = 0
		m.mu.Unlock()
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "disengage",
			"ok":  true,
		})
	case "feed_hold":
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": "feed_hold",
			"ok":  true,
		})
	default:
		ack, _ = json.Marshal(map[string]interface{}{
			"ack": parsed.Cmd,
			"ok":  false,
			"err": "unknown command",
		})
	}

	select {
	case m.statusCh <- ack:
	default:
	}
}

func (m *Manager) simConfigValue(key string) interface{} {
	defaults := map[string]interface{}{
		"spindle_ppr":            1000,
		"spindle_quadrature":     4,
		"spindle_counts_per_rev": 4000,
		"spindle_max_rpm":        3500,
		"z_scale_resolution_mm":  0.005,
		"z_travel_min_mm":        -300.0,
		"z_travel_max_mm":        0.0,
		"z_leadscrew_pitch_mm":   6.0,
		"z_steps_per_rev":        1000,
		"z_belt_ratio":           1.0,
		"z_steps_per_mm":         166.667,
		"z_max_speed_mm_s":       50.0,
		"z_accel_mm_s2":          100.0,
		"z_backlash_mm":          0.0,
		"x_scale_resolution_mm":  0.005,
		"x_is_diameter":          true,
		"x_travel_min_mm":        -75.0,
		"x_travel_max_mm":        0.0,
		"x_steps_per_rev":        1000,
		"x_leadscrew_pitch_mm":   3.0,
		"x_belt_ratio":           1.0,
		"x_steps_per_mm":         333.333,
		"thread_retract_mode":    0,
		"thread_retract_x_mm":    1.0,
		"thread_compound_angle":  29.0,
	}
	if v, ok := defaults[key]; ok {
		return v
	}
	return 0
}
