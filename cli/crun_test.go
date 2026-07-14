package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"sync/atomic"
	"testing"
	"time"
)

func TestExitCodeMap(t *testing.T) {
	cases := []struct {
		state, category string
		want            int
	}{
		{"succeeded", "", 0},
		{"dead_lettered", "app-error", 20},
		{"dead_lettered", "crash", 21},
		{"dead_lettered", "oom", 22},
		{"dead_lettered", "preempted", 23},
		{"dead_lettered", "deadline", 24},
		{"dead_lettered", "spawn-failure", 25},
		{"dead_lettered", "upload-failure", 26},
		{"dead_lettered", "lost", 27},
		{"dead_lettered", "", 29},
		{"dead_lettered", "not-a-category", 29},
	}
	for _, c := range cases {
		if got := exitForTerminal(c.state, c.category); got != c.want {
			t.Errorf("exitForTerminal(%q, %q) = %d, want %d", c.state, c.category, got, c.want)
		}
	}
}

// stubController serves the slice of the control protocol the tests need: a
// fixed job status that can be swapped mid-test, submits, queue depths, and
// an artifact download.
type stubController struct {
	status   atomic.Value // jobStatus
	polls    atomic.Int64
	artifact atomic.Value // []byte served from /v1/jobs/42/result
}

func (s *stubController) handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /v1/jobs/42/result", func(w http.ResponseWriter, r *http.Request) {
		body, _ := s.artifact.Load().([]byte)
		if body == nil {
			w.WriteHeader(http.StatusNotFound)
			fmt.Fprint(w, `{"error": "no result artifact for this job"}`)
			return
		}
		st := s.status.Load().(jobStatus)
		w.Header().Set("X-Checksum-Sha256", st.ArtifactSha256)
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Write(body)
	})
	mux.HandleFunc("GET /v1/jobs/42", func(w http.ResponseWriter, r *http.Request) {
		s.polls.Add(1)
		json.NewEncoder(w).Encode(s.status.Load().(jobStatus))
	})
	mux.HandleFunc("GET /v1/jobs/", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNotFound)
		fmt.Fprint(w, `{"error": "unknown job"}`)
	})
	mux.HandleFunc("POST /v1/jobs", func(w http.ResponseWriter, r *http.Request) {
		var spec map[string]interface{}
		if json.NewDecoder(r.Body).Decode(&spec) != nil || spec["job_id"] == nil {
			w.WriteHeader(http.StatusBadRequest)
			fmt.Fprint(w, `{"error": "invalid jobspec"}`)
			return
		}
		fmt.Fprintf(w, `{"job_id": %v, "state": "pending"}`, spec["job_id"])
	})
	mux.HandleFunc("GET /v1/queue", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, `{"pending": 3, "pending_by_priority": {"high": 1, "normal": 2, "low": 0},
		                "in_flight": 2, "done": 40, "dead_lettered": 1,
		                "waiting": {"9": "no worker satisfies feature:gpu"}}`)
	})
	return mux
}

func newStub(t *testing.T, initial jobStatus) (*stubController, *client) {
	t.Helper()
	stub := &stubController{}
	stub.status.Store(initial)
	srv := httptest.NewServer(stub.handler())
	t.Cleanup(srv.Close)
	return stub, newClient(srv.URL)
}

func TestJobStatusParsing(t *testing.T) {
	worker := uint32(7)
	_, c := newStub(t, jobStatus{
		JobID: 42, State: "running", Attempts: 1, AttemptsMax: 3,
		WorkerID: &worker, Detail: "app-error: exit code 3",
		Category: "app-error", Checkpoint: "s3://ck/42/10",
	})
	st, err := c.job(42)
	if err != nil {
		t.Fatal(err)
	}
	if st.State != "running" || st.WorkerID == nil || *st.WorkerID != 7 {
		t.Fatalf("bad parse: %+v", st)
	}
	if st.Category != "app-error" || st.Checkpoint != "s3://ck/42/10" {
		t.Fatalf("bad parse: %+v", st)
	}
	if _, err := c.job(9); err == nil {
		t.Fatal("expected 404 for unknown job")
	} else if ae, ok := err.(*apiError); !ok || ae.status != 404 {
		t.Fatalf("want apiError 404, got %v", err)
	}
}

func TestQueueParsing(t *testing.T) {
	_, c := newStub(t, jobStatus{})
	q, err := c.queue()
	if err != nil {
		t.Fatal(err)
	}
	if q.Pending != 3 || q.InFlight != 2 || q.Done != 40 || q.DeadLettered != 1 {
		t.Fatalf("bad depths: %+v", q)
	}
	if q.Waiting["9"] != "no worker satisfies feature:gpu" {
		t.Fatalf("bad waiting map: %+v", q.Waiting)
	}
	if q.PendingByPriority["high"] != 1 || q.PendingByPriority["normal"] != 2 {
		t.Fatalf("bad per-priority depths: %+v", q.PendingByPriority)
	}
}

func TestSubmitReply(t *testing.T) {
	_, c := newStub(t, jobStatus{})
	r, err := c.submit([]byte(`{"job_id": 42, "command": ["true"]}`))
	if err != nil {
		t.Fatal(err)
	}
	if r.JobID != 42 || r.State != "pending" {
		t.Fatalf("bad reply: %+v", r)
	}
	if _, err := c.submit([]byte(`{}`)); err == nil {
		t.Fatal("expected 400 for a spec without job_id")
	}
}

func TestWatchReachesTerminalState(t *testing.T) {
	old := watchPollInterval
	watchPollInterval = time.Millisecond
	defer func() { watchPollInterval = old }()

	stub, c := newStub(t, jobStatus{JobID: 42, State: "running", AttemptsMax: 3})
	go func() {
		for stub.polls.Load() < 3 {
			time.Sleep(time.Millisecond)
		}
		stub.status.Store(jobStatus{
			JobID: 42, State: "dead_lettered", Attempts: 3, AttemptsMax: 3,
			Detail: "oom: killed", Category: "oom",
		})
	}()
	if got := watchJob(c, 42, 10*time.Second); got != 22 {
		t.Fatalf("watch of an oom dead-letter = %d, want 22", got)
	}

	stub.status.Store(jobStatus{JobID: 42, State: "succeeded", Attempts: 1, AttemptsMax: 3})
	if got := watchJob(c, 42, 10*time.Second); got != 0 {
		t.Fatalf("watch of a success = %d, want 0", got)
	}
}

func TestWatchTimesOut(t *testing.T) {
	old := watchPollInterval
	watchPollInterval = time.Millisecond
	defer func() { watchPollInterval = old }()

	_, c := newStub(t, jobStatus{JobID: 42, State: "running", AttemptsMax: 3})
	if got := watchJob(c, 42, 20*time.Millisecond); got != exitTimeout {
		t.Fatalf("watch of a stuck job = %d, want %d", got, exitTimeout)
	}
}

func TestFetchVerifiesChecksum(t *testing.T) {
	body := []byte("artifact payload")
	sum := sha256.Sum256(body)
	stub, c := newStub(t, jobStatus{
		JobID: 42, State: "succeeded", Attempts: 1, AttemptsMax: 1,
		Artifact: "s3://results/42.bin", ArtifactSha256: hex.EncodeToString(sum[:]),
	})
	stub.artifact.Store(body)

	got, headerSum, err := c.fetchResult(42)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(body) || headerSum != hex.EncodeToString(sum[:]) {
		t.Fatalf("bad download: %q header %q", got, headerSum)
	}

	out := t.TempDir() + "/artifact.bin"
	if code := cmdFetch([]string{"42", "--controller", c.base, "--output", out}); code != 0 {
		t.Fatalf("fetch of a good artifact = %d, want 0", code)
	}
	written, err := os.ReadFile(out)
	if err != nil || string(written) != string(body) {
		t.Fatalf("written artifact drifted: %q %v", written, err)
	}
}

func TestFetchRejectsCorruptedArtifact(t *testing.T) {
	body := []byte("artifact payload")
	sum := sha256.Sum256(body)
	stub, c := newStub(t, jobStatus{
		JobID: 42, State: "succeeded", Attempts: 1, AttemptsMax: 1,
		Artifact: "s3://results/42.bin", ArtifactSha256: hex.EncodeToString(sum[:]),
	})
	stub.artifact.Store([]byte("tampered payload"))

	out := t.TempDir() + "/artifact.bin"
	if code := cmdFetch([]string{"42", "--controller", c.base, "--output", out}); code != exitChecksum {
		t.Fatalf("fetch of a corrupted artifact = %d, want %d", code, exitChecksum)
	}
	if _, err := os.Stat(out); err == nil {
		t.Fatal("corrupted artifact must not be written")
	}
}

func TestTransportErrorExitsThree(t *testing.T) {
	srv := httptest.NewServer(http.NotFoundHandler())
	base := srv.URL
	srv.Close() // nothing listens here any more
	if got := watchJob(newClient(base), 42, time.Second); got != exitTransport {
		t.Fatalf("watch against a dead controller = %d, want %d", got, exitTransport)
	}
}

func TestApplyOverrides(t *testing.T) {
	out, err := applyOverrides([]byte(`{"job_id": 1, "command": ["true"]}`), "aws", 2, "high", "acme")
	if err != nil {
		t.Fatal(err)
	}
	var spec jobSpec
	if err := json.Unmarshal(out, &spec); err != nil {
		t.Fatal(err)
	}
	if spec.Requires.NodeClass != "aws" {
		t.Fatalf("backend override lost: %s", out)
	}
	if spec.AttemptsMax != 3 {
		t.Fatalf("max-retries 2 should give attempts_max 3, got %d", spec.AttemptsMax)
	}
	if spec.Priority != "high" || spec.Tenant != "acme" {
		t.Fatalf("priority overrides lost: %s", out)
	}

	// No flags means the spec passes through untouched.
	raw := []byte(`{"job_id": 1}`)
	same, err := applyOverrides(raw, "", -1, "", "")
	if err != nil || string(same) != string(raw) {
		t.Fatalf("passthrough changed the spec: %s", same)
	}
}
