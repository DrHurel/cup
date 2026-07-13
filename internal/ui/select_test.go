package ui

import "testing"

func TestIndexOf(t *testing.T) {
	options := []string{"a", "b", "c"}
	cases := map[string]int{
		"a": 0,
		"b": 1,
		"c": 2,
		"z": 0, // absent -> 0
		"":  0,
	}
	for want, idx := range cases {
		if got := indexOf(options, want); got != idx {
			t.Errorf("indexOf(%q) = %d, want %d", want, got, idx)
		}
	}
}

func TestIsUpKey(t *testing.T) {
	up := []struct {
		buf []byte
		n   int
	}{
		{[]byte{'k', 0, 0}, 1},    // vim up
		{[]byte{27, '[', 'A'}, 3}, // arrow up escape sequence
	}
	for _, c := range up {
		if !isUpKey(c.buf, c.n) {
			t.Errorf("isUpKey(%v, %d) = false, want true", c.buf, c.n)
		}
	}

	notUp := []struct {
		buf []byte
		n   int
	}{
		{[]byte{'j', 0, 0}, 1},    // down key
		{[]byte{27, '[', 'B'}, 3}, // arrow down
		{[]byte{'x', 0, 0}, 1},    // unrelated
		{[]byte{27, '[', 'A'}, 1}, // right bytes but wrong length
	}
	for _, c := range notUp {
		if isUpKey(c.buf, c.n) {
			t.Errorf("isUpKey(%v, %d) = true, want false", c.buf, c.n)
		}
	}
}

func TestIsDownKey(t *testing.T) {
	down := []struct {
		buf []byte
		n   int
	}{
		{[]byte{'j', 0, 0}, 1},
		{[]byte{27, '[', 'B'}, 3},
	}
	for _, c := range down {
		if !isDownKey(c.buf, c.n) {
			t.Errorf("isDownKey(%v, %d) = false, want true", c.buf, c.n)
		}
	}

	notDown := []struct {
		buf []byte
		n   int
	}{
		{[]byte{'k', 0, 0}, 1},
		{[]byte{27, '[', 'A'}, 3},
		{[]byte{27, '[', 'B'}, 1}, // wrong length
	}
	for _, c := range notDown {
		if isDownKey(c.buf, c.n) {
			t.Errorf("isDownKey(%v, %d) = true, want false", c.buf, c.n)
		}
	}
}

func TestSelectNumbered(t *testing.T) {
	options := []string{"red", "green", "blue"}

	// A valid numbered choice returns the matching option.
	feedStdin(t, "2\n")
	got, err := selectNumbered("pick?", options)
	if err != nil {
		t.Fatalf("selectNumbered error: %v", err)
	}
	if got != "green" {
		t.Errorf("selectNumbered = %q, want %q", got, "green")
	}

	// Out-of-range then valid: it repeats until a legal index.
	feedStdin(t, "9\n1\n")
	got, err = selectNumbered("pick?", options)
	if err != nil {
		t.Fatalf("selectNumbered (retry) error: %v", err)
	}
	if got != "red" {
		t.Errorf("selectNumbered = %q, want %q", got, "red")
	}
}

func TestSelectNumberedAbort(t *testing.T) {
	feedStdin(t, "") // EOF -> underlying Text aborts
	_, err := selectNumbered("pick?", []string{"a", "b"})
	if err == nil {
		t.Error("selectNumbered on EOF = nil error, want abort error")
	}
}

// Select with an empty option list is an immediate error, no terminal needed.
func TestSelectNoOptions(t *testing.T) {
	if _, err := Select("pick?", nil, ""); err == nil {
		t.Error("Select(no options) = nil error, want error")
	}
}
