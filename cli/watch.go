package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"time"
)

// watchPollInterval is a variable so the tests can poll fast.
var watchPollInterval = 500 * time.Millisecond

func cmdWatch(args []string) int {
	fs := flag.NewFlagSet("watch", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	timeout := fs.Int("timeout", 0, "seconds to wait for a terminal state (0 = no limit)")
	pos, err := parseMixed(fs, args)
	if err != nil || len(pos) != 1 {
		fmt.Fprintln(os.Stderr, "usage: crun watch <job-id> [--timeout S]")
		return exitUsage
	}
	id, err := strconv.ParseUint(pos[0], 10, 64)
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: bad job id %q\n", pos[0])
		return exitUsage
	}
	return watchJob(newClient(*controller), id, time.Duration(*timeout)*time.Second)
}

// watchJob polls the job until it reaches a terminal state, printing each
// state transition, and returns the contract exit code. A zero timeout means
// poll forever.
func watchJob(c *client, id uint64, timeout time.Duration) int {
	deadline := time.Time{}
	if timeout > 0 {
		deadline = time.Now().Add(timeout)
	}
	last := ""
	for {
		st, err := c.job(id)
		if err != nil {
			if ae, ok := err.(*apiError); ok {
				fmt.Fprintf(os.Stderr, "crun: %v\n", ae)
				return exitUsage
			}
			fmt.Fprintf(os.Stderr, "crun: %v\n", err)
			return exitTransport
		}
		if st.State != last {
			line := fmt.Sprintf("job %d %s attempts=%d/%d", id, st.State, st.Attempts, st.AttemptsMax)
			if st.WorkerID != nil {
				line += fmt.Sprintf(" worker=%d", *st.WorkerID)
			}
			if st.Detail != "" {
				line += " detail=" + strconv.Quote(st.Detail)
			}
			fmt.Println(line)
			last = st.State
		}
		if isTerminal(st.State) {
			return exitForTerminal(st.State, st.Category)
		}
		if !deadline.IsZero() && time.Now().After(deadline) {
			fmt.Fprintf(os.Stderr, "crun: timed out after %s waiting on job %d\n", timeout, id)
			return exitTimeout
		}
		time.Sleep(watchPollInterval)
	}
}
