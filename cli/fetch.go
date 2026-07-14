package main

import (
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
)

const exitChecksum = 5

func cmdFetch(args []string) int {
	fs := flag.NewFlagSet("fetch", flag.ContinueOnError)
	controller := fs.String("controller", "", "controller base URL")
	output := fs.String("output", "", "path to write the artifact (default job-<id>.out)")
	pos, err := parseMixed(fs, args)
	if err != nil || len(pos) != 1 {
		fmt.Fprintln(os.Stderr, "usage: crun fetch <job-id> [--output PATH]")
		return exitUsage
	}
	id, err := strconv.ParseUint(pos[0], 10, 64)
	if err != nil {
		fmt.Fprintf(os.Stderr, "crun: bad job id %q\n", pos[0])
		return exitUsage
	}

	c := newClient(*controller)
	st, err := c.job(id)
	if err != nil {
		if ae, ok := err.(*apiError); ok {
			fmt.Fprintf(os.Stderr, "crun: %v\n", ae)
			return exitUsage
		}
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}
	if st.Artifact == "" {
		fmt.Fprintf(os.Stderr, "crun: job %d has no result artifact (state %s)\n", id, st.State)
		return exitUsage
	}

	body, headerSum, err := c.fetchResult(id)
	if err != nil {
		if ae, ok := err.(*apiError); ok {
			fmt.Fprintf(os.Stderr, "crun: %v\n", ae)
			return exitUsage
		}
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitTransport
	}

	sum := sha256.Sum256(body)
	got := hex.EncodeToString(sum[:])
	want := st.ArtifactSha256
	if want == "" {
		want = headerSum
	}
	if got != want {
		fmt.Fprintf(os.Stderr, "crun: checksum mismatch for job %d: want %s, got %s\n", id, want, got)
		return exitChecksum
	}

	path := *output
	if path == "" {
		path = fmt.Sprintf("job-%d.out", id)
	}
	if err := os.WriteFile(path, body, 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "crun: %v\n", err)
		return exitUsage
	}
	fmt.Printf("job %d artifact %s: %d bytes, sha256 verified, written to %s\n",
		id, st.Artifact, len(body), path)
	return exitOK
}

// fetchResult downloads the raw artifact bytes and the checksum header.
func (c *client) fetchResult(id uint64) ([]byte, string, error) {
	resp, err := c.http.Get(fmt.Sprintf("%s/v1/jobs/%d/result", c.base, id))
	if err != nil {
		return nil, "", err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, "", err
	}
	if resp.StatusCode != http.StatusOK {
		return nil, "", &apiError{status: resp.StatusCode, message: string(data)}
	}
	return data, resp.Header.Get("X-Checksum-Sha256"), nil
}
