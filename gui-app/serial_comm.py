"""
Serial communication layer between the GUI and the board.

Implements the line protocol documented in docs/protocol.md. Runs the
serial read loop on a background thread and hands parsed events to the
GUI thread via a queue.Queue, so app.py never blocks on I/O.

Swap in a different transport later (e.g. a mock board for GUI-only
development) by implementing the same connect/send/close interface.
"""

import queue
import threading
import time

import serial
import serial.tools.list_ports

DEFAULT_BAUD = 115200


def list_ports():
    """Return a list of available serial port device names."""
    return [p.device for p in serial.tools.list_ports.comports()]


def parse_line(line):
    """
    Parse one protocol line into an event dict, or None if unrecognized.

    See docs/protocol.md for the full message format.
    """
    line = line.strip()
    if not line:
        return None

    if line == "READY":
        return {"type": "ready"}
    if line == "PONG":
        return {"type": "pong"}

    parts = line.split(",")
    tag = parts[0]

    if tag == "BEGIN" and len(parts) == 2:
        try:
            return {"type": "begin", "count": int(parts[1])}
        except ValueError:
            return None

    if tag == "TEST" and len(parts) == 5:
        _, name, test_type, result, value = parts
        return {
            "type": "test_result",
            "name": name,
            "test_type": test_type,
            "result": result,
            "value": value,
        }

    if tag == "RESULT" and len(parts) == 3 and parts[1] == "OVERALL":
        return {"type": "overall_result", "result": parts[2]}

    if tag == "FAN_ACK" and len(parts) == 2:
        try:
            return {"type": "fan_ack", "percent": int(parts[1])}
        except ValueError:
            return None

    # Unrecognized line - caller decides whether to log it raw.
    return {"type": "raw", "line": line}


class SerialTestClient:
    """
    Owns the serial connection and the background read thread.
    Parsed events are pushed onto self.events for the GUI to poll.
    """

    def __init__(self):
        self._ser = None
        self._reader_thread = None
        self._stop_flag = threading.Event()
        self.events = queue.Queue()

    def is_connected(self):
        return self._ser is not None and self._ser.is_open

    def connect(self, port, baud=DEFAULT_BAUD, timeout=1):
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=timeout)
        self._stop_flag.clear()
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

    def disconnect(self):
        self._stop_flag.set()
        if self._reader_thread is not None:
            self._reader_thread.join(timeout=2)
        if self._ser is not None:
            self._ser.close()
        self._ser = None

    def send(self, command):
        """Send a bare command word, e.g. 'START' or 'PING'."""
        if not self.is_connected():
            raise RuntimeError("Not connected to board")
        self._ser.write((command.strip() + "\n").encode("ascii"))

    def start_test(self):
        self.send("START")

    def ping(self):
        self.send("PING")

    def set_fan_speed(self, percent):
        """percent: 0-100, 0 = fan off. Independent of start_test()."""
        self.send(f"FAN_SET,{int(percent)}")

    def _read_loop(self):
        while not self._stop_flag.is_set():
            try:
                raw = self._ser.readline()
            except serial.SerialException:
                self.events.put({"type": "disconnected"})
                return
            if not raw:
                continue  # timeout, no data - loop again
            try:
                line = raw.decode("ascii", errors="replace")
            except UnicodeDecodeError:
                continue
            event = parse_line(line)
            if event is not None:
                self.events.put(event)


class MockTestClient(SerialTestClient):
    """
    Fakes a board so the GUI can be developed/demoed without hardware.
    Same interface as SerialTestClient; connect() ignores the port.
    """

    _MOCK_TESTS = [
        ("GPIO_IN_0", "GPIO", "PASS", "1"),
        ("GPIO_OUT_0", "GPIO", "PASS", "1"),
        ("ADC_CH0", "ADC", "PASS", "2048"),
        ("LED_STATUS", "LED", "PASS", "NA"),
    ]

    def connect(self, port, baud=DEFAULT_BAUD, timeout=1):
        self._ser = "mock"  # sentinel, not a real serial.Serial - see is_connected() override below
        self.events.put({"type": "ready"})

    def disconnect(self):
        self._ser = None

    def is_connected(self):
        return self._ser is not None

    def send(self, command):
        command = command.strip()
        if command == "START":
            threading.Thread(target=self._run_mock_sequence, daemon=True).start()
        elif command.startswith("FAN_SET,"):
            percent = int(command.split(",")[1])
            self.events.put({"type": "fan_ack", "percent": percent})

    def _run_mock_sequence(self):
        self.events.put({"type": "begin", "count": len(self._MOCK_TESTS)})
        overall = "PASS"
        for name, test_type, result, value in self._MOCK_TESTS:
            time.sleep(0.3)
            if result == "FAIL":
                overall = "FAIL"
            self.events.put(
                {
                    "type": "test_result",
                    "name": name,
                    "test_type": test_type,
                    "result": result,
                    "value": value,
                }
            )
        self.events.put({"type": "overall_result", "result": overall})
