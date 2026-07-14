package main

import (
	"flag"
	"fmt"
	"net/http"
	"os"
	"strconv"
)

func cmdDrainWorker(args []string) int {
	fs := flag.NewFlagSet("drain-worker", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	pos, err := parseMixed(fs, args)
	if err != nil || len(pos) != 1 {
		fmt.Fprintln(os.Stderr, "usage: crun drain-worker <worker-id>")
		return exitUsage
	}
	id, err := strconv.ParseUint(pos[0], 10, 16)
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: bad worker id %q\n", pos[0])
		return exitUsage
	}

	c := newClient(*controller)
	var reply struct {
		WorkerID uint32 `json:"worker_id"`
		Draining bool   `json:"draining"`
	}
	err = c.do(http.MethodPost, fmt.Sprintf("/v1/workers/%d/drain", id), []byte("{}"), &reply)
	if err != nil {
		if ae, ok := err.(*apiError); ok {
			fmt.Fprintf(os.Stderr, "crun: %v\n", ae)
			return exitUsage
		}
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}
	fmt.Printf("worker %d draining: finishes its current job, takes nothing new\n", reply.WorkerID)
	return exitOK
}
