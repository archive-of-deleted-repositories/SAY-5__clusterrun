package main

import (
	"encoding/json"
	"os"
	"reflect"
	"testing"
)

// The golden files under tests/golden/ are emitted by the C++ codec (see
// tests/codec_dump.cpp). Round-tripping them through Go types proves the two
// sides agree on the wire schema.

type jobRequires struct {
	CPUMin    uint32   `json:"cpu_min"`
	MemGBMin  uint32   `json:"mem_gb_min"`
	Features  []string `json:"features"`
	NodeClass string   `json:"node_class"`
}

type jobSpec struct {
	JobID              uint64      `json:"job_id"`
	Command            []string    `json:"command"`
	StdoutS3           string      `json:"stdout_s3"`
	StderrS3           string      `json:"stderr_s3"`
	Requires           jobRequires `json:"requires"`
	AttemptsMax        uint32      `json:"attempts_max"`
	CheckpointInterval uint32      `json:"checkpoint_interval_s"`
	IdempotencyKey     string      `json:"idempotency_key"`
	Priority           string      `json:"priority"`
	Tenant             string      `json:"tenant"`
	ResumeFromCkpt     string      `json:"resume_from_checkpoint_s3,omitempty"`
}

type capabilitySpec struct {
	WorkerID  uint32   `json:"worker_id"`
	CPU       uint32   `json:"cpu"`
	MemGB     uint32   `json:"mem_gb"`
	Features  []string `json:"features"`
	NodeClass string   `json:"node_class"`
}

func mustGolden(t *testing.T, name string) []byte {
	t.Helper()
	data, err := os.ReadFile("../tests/golden/" + name)
	if err != nil {
		t.Fatalf("golden %s: %v", name, err)
	}
	return data
}

func roundTrip(t *testing.T, golden []byte, typed interface{}) {
	t.Helper()
	if err := json.Unmarshal(golden, typed); err != nil {
		t.Fatalf("decode into typed struct: %v", err)
	}
	re, err := json.Marshal(typed)
	if err != nil {
		t.Fatalf("re-encode: %v", err)
	}
	var want, got map[string]interface{}
	if err := json.Unmarshal(golden, &want); err != nil {
		t.Fatalf("golden is not an object: %v", err)
	}
	if err := json.Unmarshal(re, &got); err != nil {
		t.Fatalf("re-encoded is not an object: %v", err)
	}
	if !reflect.DeepEqual(want, got) {
		t.Fatalf("round trip drifted\n golden: %s\n go:     %s", golden, re)
	}
}

func TestJobGoldenRoundTrip(t *testing.T) {
	var spec jobSpec
	roundTrip(t, mustGolden(t, "job.json"), &spec)
	if spec.JobID != 42 || spec.Requires.NodeClass != "cpu-heavy" || spec.AttemptsMax != 3 {
		t.Fatalf("unexpected decoded fields: %+v", spec)
	}
	if spec.Priority != "high" || spec.Tenant != "acme" {
		t.Fatalf("priority fields drifted: %+v", spec)
	}
}

func TestCapabilityGoldenRoundTrip(t *testing.T) {
	var spec capabilitySpec
	roundTrip(t, mustGolden(t, "capability.json"), &spec)
	if spec.WorkerID != 7 || spec.NodeClass != "aws" {
		t.Fatalf("unexpected decoded fields: %+v", spec)
	}
}
