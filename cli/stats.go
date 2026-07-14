package main

import (
	"flag"
	"fmt"
	"net/http"
	"os"
	"sort"
	"strconv"
)

type statsReply struct {
	Submitted         int            `json:"submitted"`
	Done              int            `json:"done"`
	DeadLettered      int            `json:"dead_lettered"`
	Pending           int            `json:"pending"`
	InFlight          int            `json:"in_flight"`
	RetriesByCategory map[string]int `json:"retries_by_category"`
	LatencyP50Us      uint64         `json:"latency_p50_us"`
	LatencyP95Us      uint64         `json:"latency_p95_us"`
	ThroughputPerS    float64        `json:"throughput_per_s"`
}

func (c *client) stats() (statsReply, error) {
	var s statsReply
	err := c.do(http.MethodGet, "/v1/stats", nil, &s)
	return s, err
}

func cmdStats(args []string) int {
	fs := flag.NewFlagSet("stats", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	pos, err := parseMixed(fs, args)
	if err != nil || len(pos) != 0 {
		fmt.Fprintln(os.Stderr, "usage: crun stats")
		return exitUsage
	}

	s, err := newClient(*controller).stats()
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}
	fmt.Printf("submitted        %d\n", s.Submitted)
	fmt.Printf("done             %d\n", s.Done)
	fmt.Printf("dead_lettered    %d\n", s.DeadLettered)
	fmt.Printf("pending          %d\n", s.Pending)
	fmt.Printf("in_flight        %d\n", s.InFlight)
	fmt.Printf("throughput       %.2f jobs/s\n", s.ThroughputPerS)
	fmt.Printf("latency p50      %s\n", formatUs(s.LatencyP50Us))
	fmt.Printf("latency p95      %s\n", formatUs(s.LatencyP95Us))
	if len(s.RetriesByCategory) > 0 {
		fmt.Println("retries by category:")
		cats := make([]string, 0, len(s.RetriesByCategory))
		for cat := range s.RetriesByCategory {
			cats = append(cats, cat)
		}
		sort.Strings(cats)
		for _, cat := range cats {
			fmt.Printf("  %-15s %d\n", cat, s.RetriesByCategory[cat])
		}
	}
	return exitOK
}

func formatUs(us uint64) string {
	switch {
	case us >= 1_000_000:
		return fmt.Sprintf("%.2fs", float64(us)/1e6)
	case us >= 1_000:
		return fmt.Sprintf("%.2fms", float64(us)/1e3)
	default:
		return strconv.FormatUint(us, 10) + "us"
	}
}
