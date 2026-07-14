package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

// client wraps the controller's HTTP control endpoint (see PROTOCOL.md).
type client struct {
	base string
	http *http.Client
}

func newClient(base string) *client {
	if base == "" {
		base = os.Getenv("CRUN_CONTROLLER")
	}
	if base == "" {
		base = "http://127.0.0.1:8447"
	}
	return &client{
		base: strings.TrimRight(base, "/"),
		http: &http.Client{Timeout: 10 * time.Second},
	}
}

// apiError is a non-2xx response from the controller. It is distinct from a
// transport error (connection refused, timeout), which surfaces as exit 3.
type apiError struct {
	status  int
	message string
}

func (e *apiError) Error() string {
	return fmt.Sprintf("controller returned %d: %s", e.status, e.message)
}

type jobStatus struct {
	JobID          uint64  `json:"job_id"`
	State          string  `json:"state"`
	Attempts       uint32  `json:"attempts"`
	AttemptsMax    uint32  `json:"attempts_max"`
	WorkerID       *uint32 `json:"worker_id"`
	Detail         string  `json:"detail"`
	Category       string  `json:"category"`
	Checkpoint     string  `json:"checkpoint"`
	Artifact       string  `json:"artifact"`
	ArtifactSha256 string  `json:"artifact_sha256"`
}

type queueDepths struct {
	Pending           int               `json:"pending"`
	PendingByPriority map[string]int    `json:"pending_by_priority"`
	InFlight          int               `json:"in_flight"`
	Done              int               `json:"done"`
	DeadLettered      int               `json:"dead_lettered"`
	Waiting           map[string]string `json:"waiting"`
}

type submitReply struct {
	JobID uint64 `json:"job_id"`
	State string `json:"state"`
}

func (c *client) do(method, path string, body []byte, out interface{}) error {
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, err := http.NewRequest(method, c.base+path, rd)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := c.http.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	if resp.StatusCode != http.StatusOK {
		msg := resp.Status
		var e struct {
			Error string `json:"error"`
		}
		if json.Unmarshal(data, &e) == nil && e.Error != "" {
			msg = e.Error
		}
		return &apiError{status: resp.StatusCode, message: msg}
	}
	if out != nil {
		return json.Unmarshal(data, out)
	}
	return nil
}

func (c *client) submit(spec []byte) (submitReply, error) {
	var r submitReply
	err := c.do(http.MethodPost, "/v1/jobs", spec, &r)
	return r, err
}

func (c *client) job(id uint64) (jobStatus, error) {
	var s jobStatus
	err := c.do(http.MethodGet, fmt.Sprintf("/v1/jobs/%d", id), nil, &s)
	return s, err
}

func (c *client) queue() (queueDepths, error) {
	var q queueDepths
	err := c.do(http.MethodGet, "/v1/queue", nil, &q)
	return q, err
}
