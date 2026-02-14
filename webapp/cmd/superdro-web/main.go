package main

import (
	"context"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/lunarforge/superdro-web/internal/serial"
	"github.com/lunarforge/superdro-web/internal/ws"
	"go.uber.org/zap"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	// CLI flags
	port := flag.String("port", "", "Serial port path (default: auto-detect)")
	baud := flag.Int("baud", 115200, "Serial baud rate")
	addr := flag.String("addr", ":8080", "HTTP listen address")
	sim := flag.Bool("sim", false, "Simulated mode (no serial device needed)")
	staticDir := flag.String("static", "", "Path to static files (default: auto-detect)")
	logLevel := flag.String("log", "info", "Log level: debug, info, warn, error")
	flag.Parse()

	// Logger
	var logger *zap.Logger
	var err error
	switch *logLevel {
	case "debug":
		cfg := zap.NewDevelopmentConfig()
		logger, err = cfg.Build()
	default:
		cfg := zap.NewProductionConfig()
		switch *logLevel {
		case "warn":
			cfg.Level.SetLevel(zap.WarnLevel)
		case "error":
			cfg.Level.SetLevel(zap.ErrorLevel)
		default:
			cfg.Level.SetLevel(zap.InfoLevel)
		}
		logger, err = cfg.Build()
	}
	if err != nil {
		return fmt.Errorf("init logger: %w", err)
	}
	defer logger.Sync()

	// Resolve static directory
	staticPath := *staticDir
	if staticPath == "" {
		// Try relative to executable, then relative to CWD
		exe, _ := os.Executable()
		candidates := []string{
			filepath.Join(filepath.Dir(exe), "..", "..", "static"),
			filepath.Join(filepath.Dir(exe), "static"),
			"static",
			filepath.Join("webapp", "static"),
		}
		for _, c := range candidates {
			if info, err := os.Stat(c); err == nil && info.IsDir() {
				staticPath = c
				break
			}
		}
		if staticPath == "" {
			return fmt.Errorf("cannot find static directory; use -static flag")
		}
	}
	logger.Info("Serving static files", zap.String("path", staticPath))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Serial manager
	mgr := serial.NewManager(serial.Config{
		PortPath: *port,
		BaudRate: *baud,
		Simulate: *sim,
	}, logger)

	// WebSocket hub
	hub := ws.NewHub(logger)

	// Wire: serial status → WebSocket broadcast
	go func() {
		for {
			select {
			case msg := <-mgr.StatusCh():
				hub.Broadcast(msg)
			case <-ctx.Done():
				return
			}
		}
	}()

	// Wire: WebSocket commands → serial write
	go func() {
		for {
			select {
			case cmd := <-hub.CommandCh():
				mgr.Send(cmd)
			case <-ctx.Done():
				return
			}
		}
	}()

	// Start serial manager
	go mgr.Run(ctx)

	// Start WebSocket hub
	go hub.Run(ctx)

	// HTTP routes
	mux := http.NewServeMux()
	mux.HandleFunc("/ws", hub.ServeWS)
	mux.Handle("/", http.FileServer(http.Dir(staticPath)))

	server := &http.Server{
		Addr:    *addr,
		Handler: mux,
	}

	// Start HTTP server
	go func() {
		logger.Info("HTTP server starting",
			zap.String("addr", *addr),
			zap.Bool("sim", *sim),
		)
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Error("HTTP server error", zap.Error(err))
		}
	}()

	if *sim {
		logger.Info("Running in simulated mode")
	} else if *port != "" {
		logger.Info("Using serial port", zap.String("port", *port))
	} else {
		logger.Info("Auto-detecting serial port")
	}

	// Wait for signal
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	sig := <-sigCh
	logger.Info("Shutting down", zap.String("signal", sig.String()))

	cancel()

	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutdownCancel()
	return server.Shutdown(shutdownCtx)
}
