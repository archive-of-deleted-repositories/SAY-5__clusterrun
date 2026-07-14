package main

// Exit codes for dead-lettered jobs, keyed by the failure category token the
// controller reports. The mapping is part of the contract in PROTOCOL.md.
var categoryExit = map[string]int{
	"app-error":      20,
	"crash":          21,
	"oom":            22,
	"preempted":      23,
	"deadline":       24,
	"spawn-failure":  25,
	"upload-failure": 26,
	"lost":           27,
}

const exitDeadLetterUnknown = 29

// exitForTerminal maps a terminal job state to the crun exit code.
func exitForTerminal(state, category string) int {
	if state == "succeeded" {
		return exitOK
	}
	if code, ok := categoryExit[category]; ok {
		return code
	}
	return exitDeadLetterUnknown
}

func isTerminal(state string) bool {
	return state == "succeeded" || state == "dead_lettered"
}
