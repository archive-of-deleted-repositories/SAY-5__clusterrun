// crun is the command line client for the clusterrun controller. It speaks
// the HTTP control protocol described in PROTOCOL.md and exits with the
// contract codes defined there so scripts can branch on failure category.
package main

import (
	"fmt"
	"os"
)

const (
	exitOK        = 0
	exitUsage     = 2
	exitTransport = 3
	exitTimeout   = 4
)

func usage() {
	fmt.Fprintf(os.Stderr, `usage: crun <command> [args]

commands:
  submit <jobspec.json>   submit a job descriptor
  queue                   show queue depths
  watch <job-id>          poll a job to its terminal state
  fetch <job-id>          download and verify a result artifact

The controller address comes from --controller or CRUN_CONTROLLER
(default http://127.0.0.1:8447).
`)
}

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(exitUsage)
	}
	var code int
	switch os.Args[1] {
	case "submit":
		code = cmdSubmit(os.Args[2:])
	case "queue":
		code = cmdQueue(os.Args[2:])
	case "watch":
		code = cmdWatch(os.Args[2:])
	case "fetch":
		code = cmdFetch(os.Args[2:])
	case "help", "-h", "--help":
		usage()
		code = exitOK
	default:
		fmt.Fprintf(os.Stderr, "crun: unknown command %q\n", os.Args[1])
		usage()
		code = exitUsage
	}
	os.Exit(code)
}
