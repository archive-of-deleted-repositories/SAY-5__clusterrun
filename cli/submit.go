package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"time"
)

// parseMixed parses fs against args, allowing flags before or after
// positional arguments (the flag package normally stops at the first
// non-flag token). Returns the positional arguments in order.
func parseMixed(fs *flag.FlagSet, args []string) ([]string, error) {
	var pos []string
	rest := args
	for {
		if err := fs.Parse(rest); err != nil {
			return nil, err
		}
		if fs.NArg() == 0 {
			return pos, nil
		}
		pos = append(pos, fs.Arg(0))
		rest = fs.Args()[1:]
	}
}

func cmdSubmit(args []string) int {
	fs := flag.NewFlagSet("submit", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	backend := fs.String("backend", "", "pin the job to a backend: aws or local")
	maxRetries := fs.Int("max-retries", -1, "retries after the first attempt")
	timeout := fs.Int("timeout", 0, "seconds to wait for the job with --wait (0 = no limit)")
	wait := fs.Bool("wait", false, "watch the job to its terminal state after submitting")

	pos, err := parseMixed(fs, args)
	if err != nil {
		return exitUsage
	}
	if len(pos) != 1 {
		fmt.Fprintln(os.Stderr, "usage: crun submit <jobspec.json> [--backend aws|local] [--max-retries N] [--timeout S] [--wait]")
		return exitUsage
	}
	if *backend != "" && *backend != "aws" && *backend != "local" {
		fmt.Fprintf(os.Stderr, "crun: bad --backend %q (want aws or local)\n", *backend)
		return exitUsage
	}

	raw, err := os.ReadFile(pos[0])
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitUsage
	}
	spec, err := applyOverrides(raw, *backend, *maxRetries)
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitUsage
	}

	c := newClient(*controller)
	reply, err := c.submit(spec)
	if err != nil {
		if ae, ok := err.(*apiError); ok {
			fmt.Fprintf(os.Stderr, "crun: %v\n", ae)
			return exitUsage
		}
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}
	fmt.Printf("job %d %s\n", reply.JobID, reply.State)

	if !*wait {
		return exitOK
	}
	return watchJob(c, reply.JobID, time.Duration(*timeout)*time.Second)
}

// applyOverrides folds the submit flags into the jobspec: --backend sets
// requires.node_class and --max-retries N sets attempts_max to N+1 (the
// first attempt plus N retries).
func applyOverrides(raw []byte, backend string, maxRetries int) ([]byte, error) {
	if backend == "" && maxRetries < 0 {
		return raw, nil
	}
	var spec map[string]json.RawMessage
	if err := json.Unmarshal(raw, &spec); err != nil {
		return nil, fmt.Errorf("jobspec is not a JSON object: %w", err)
	}
	if backend != "" {
		var reqs map[string]json.RawMessage
		if r, ok := spec["requires"]; ok {
			if err := json.Unmarshal(r, &reqs); err != nil {
				return nil, fmt.Errorf("jobspec requires block: %w", err)
			}
		} else {
			reqs = map[string]json.RawMessage{}
		}
		nc, _ := json.Marshal(backend)
		reqs["node_class"] = nc
		enc, err := json.Marshal(reqs)
		if err != nil {
			return nil, err
		}
		spec["requires"] = enc
	}
	if maxRetries >= 0 {
		am, _ := json.Marshal(maxRetries + 1)
		spec["attempts_max"] = am
	}
	return json.Marshal(spec)
}
