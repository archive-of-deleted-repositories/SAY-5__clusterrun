package main

import (
	"flag"
	"fmt"
	"os"
	"sort"
)

func cmdQueue(args []string) int {
	fs := flag.NewFlagSet("queue", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	pos, err := parseMixed(fs, args)
	if err != nil || len(pos) != 0 {
		fmt.Fprintln(os.Stderr, "usage: crun queue")
		return exitUsage
	}

	c := newClient(*controller)
	q, err := c.queue()
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}
	fmt.Printf("pending        %d\n", q.Pending)
	fmt.Printf("in_flight      %d\n", q.InFlight)
	fmt.Printf("done           %d\n", q.Done)
	fmt.Printf("dead_lettered  %d\n", q.DeadLettered)
	if len(q.Waiting) > 0 {
		ids := make([]string, 0, len(q.Waiting))
		for id := range q.Waiting {
			ids = append(ids, id)
		}
		sort.Strings(ids)
		fmt.Printf("waiting        %d\n", len(q.Waiting))
		for _, id := range ids {
			fmt.Printf("  job %s: %s\n", id, q.Waiting[id])
		}
	}
	return exitOK
}
