"""
Board self-test GUI.

Connects to the board over UART, sends START, shows live per-parameter
PASS/FAIL across four panels (Temperature Test, Param Test, I/O Test, Fan),
shows one overall GREEN/RED verdict, and logs every run to gui-app/logs/.

Run:
    python app.py
    python app.py --mock     # no hardware needed, fakes a board

See docs/protocol.md for the wire format this GUI expects.
"""

import argparse
import csv
import datetime
import os
import queue
import tkinter as tk
from tkinter import ttk, messagebox

from serial_comm import SerialTestClient, MockTestClient, list_ports, DEFAULT_BAUD
from formatting import (
    VBUS_VOLTAGE_SCALE,
    DC_BUS_PRESENT_THRESHOLD_V,
    TEMP_AD_NAMES,
    PARAM_NAMES,
    BUTTON_NAMES,
    display_name,
    format_value,
    dc_bus_row,
)

LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")

POLL_INTERVAL_MS = 50
LIVE_MODE_REPEAT_DELAY_MS = 1000  # gap between auto-repeated test runs when Live Mode is on
FAN_SLIDER_DEBOUNCE_MS = 120  # coalesce rapid slider-drag events into one send


class SelfTestApp(tk.Tk):
    def __init__(self, client):
        super().__init__()
        self.title("Board Self-Test")
        self.geometry("980x680")

        self.client = client
        self.current_run_results = []
        self.expected_count = None

        # Item-id trackers, one per panel tree, so Live Mode updates rows in
        # place instead of flashing (Treeview item ids are only unique
        # within their own tree, so these can't be a single shared dict).
        self.temp_row_items = {}
        self.param_row_items = {}
        self.io_row_items = {}

        self.fan_ack_seen = False
        self._fan_slider_after_id = None

        self._build_ui()
        self._refresh_ports()
        self.after(POLL_INTERVAL_MS, self._poll_events)

    # ---- UI construction -------------------------------------------------

    def _build_ui(self):
        top = ttk.Frame(self, padding=10)
        top.pack(fill="x")

        ttk.Label(top, text="Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=20, state="readonly")
        self.port_combo.pack(side="left", padx=(4, 8))

        ttk.Button(top, text="Refresh", command=self._refresh_ports).pack(side="left")
        self.connect_btn = ttk.Button(top, text="Connect", command=self._on_connect)
        self.connect_btn.pack(side="left", padx=(8, 0))

        self.start_btn = ttk.Button(top, text="Start Test", command=self._on_start_test, state="disabled")
        self.start_btn.pack(side="right")

        self.stop_btn = ttk.Button(top, text="Stop", command=self._on_stop_test, state="disabled")
        self.stop_btn.pack(side="right", padx=(0, 8))

        self.live_mode_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(top, text="Live Mode", variable=self.live_mode_var).pack(side="right", padx=(0, 12))

        # 2x2 grid: Temperature Test | Param Test
        #           I/O Test         | Fan
        grid = ttk.Frame(self, padding=(10, 0, 10, 0))
        grid.pack(fill="both", expand=True)
        grid.columnconfigure(0, weight=1, uniform="col")
        grid.columnconfigure(1, weight=1, uniform="col")
        grid.rowconfigure(0, weight=1)
        grid.rowconfigure(1, weight=1)

        temp_frame = ttk.LabelFrame(grid, text="Temperature Test", padding=8)
        temp_frame.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)
        self.temp_tree = self._make_result_tree(temp_frame, ("param", "value", "result"),
                                                 (("param", "Parameter", 100), ("value", "Value", 90),
                                                  ("result", "Result", 70)))

        param_frame = ttk.LabelFrame(grid, text="Param Test", padding=8)
        param_frame.grid(row=0, column=1, sticky="nsew", padx=5, pady=5)
        self._build_relay_row(param_frame)
        self.param_tree = self._make_result_tree(param_frame, ("param", "value", "result"),
                                                   (("param", "Parameter", 100), ("value", "Value", 130),
                                                    ("result", "Result", 70)))

        io_frame = ttk.LabelFrame(grid, text="I/O Test", padding=8)
        io_frame.grid(row=1, column=0, sticky="nsew", padx=5, pady=5)
        self._build_io_frame(io_frame)

        fan_frame = ttk.LabelFrame(grid, text="Fan", padding=10)
        fan_frame.grid(row=1, column=1, sticky="nsew", padx=5, pady=5)
        self._build_fan_panel(fan_frame)

        debug_frame = ttk.LabelFrame(self, text="Debug Console", padding=8)
        debug_frame.pack(fill="x", padx=10, pady=(0, 5))
        self._build_debug_console(debug_frame)

        # Overall status banner
        self.status_var = tk.StringVar(value="Not connected")
        self.status_label = tk.Label(
            self, textvariable=self.status_var, font=("TkDefaultFont", 16, "bold"),
            bg="#cccccc", fg="black", pady=10,
        )
        self.status_label.pack(fill="x", padx=10, pady=10)

    def _build_debug_console(self, debug_frame):
        """
        Free-text command box + output log, right in the GUI - for commands
        that don't have a dedicated button (COIL_STATUS, BUTTON_STATUS,
        PING, or any future ones), so testing doesn't require switching to
        a separate terminal/screen session. Replies ride the same "raw"
        event path _handle_event already had (previously only printed to
        the console, not shown anywhere in the window - that's the gap
        this fills).
        """
        send_row = ttk.Frame(debug_frame)
        send_row.pack(fill="x")

        self.debug_cmd_var = tk.StringVar()
        self.debug_entry = ttk.Entry(send_row, textvariable=self.debug_cmd_var, state="disabled")
        self.debug_entry.pack(side="left", fill="x", expand=True)
        self.debug_entry.bind("<Return>", lambda _evt: self._on_send_debug_command())

        self.debug_send_btn = ttk.Button(
            send_row, text="Send", command=self._on_send_debug_command, state="disabled"
        )
        self.debug_send_btn.pack(side="left", padx=(6, 0))

        self.debug_log = tk.Text(debug_frame, height=6, state="disabled", wrap="word")
        self.debug_log.pack(fill="x", pady=(6, 0))

    def _on_send_debug_command(self):
        command = self.debug_cmd_var.get().strip()
        if not command or not self.client.is_connected():
            return
        self._append_debug_log(f"> {command}")
        self.debug_cmd_var.set("")
        try:
            self.client.send(command)
        except Exception as exc:  # e.g. board unplugged mid-type
            self._append_debug_log(f"[send failed] {exc}")

    def _append_debug_log(self, text):
        self.debug_log.config(state="normal")
        self.debug_log.insert("end", text + "\n")
        self.debug_log.see("end")
        self.debug_log.config(state="disabled")

    def _make_result_tree(self, parent, columns, column_specs):
        """
        A small Treeview used inside each panel. PASS/FAIL are colored TEXT
        (not row background) per spec: pass = green text, fail = red text.
        """
        tree = ttk.Treeview(parent, columns=columns, show="headings", height=8)
        for col, label, width in column_specs:
            tree.heading(col, text=label)
            tree.column(col, width=width, anchor="center")
        tree.tag_configure("PASS", foreground="#1a8a1a")
        tree.tag_configure("FAIL", foreground="#d32f2f")
        tree.tag_configure("INFO", foreground="#555555")
        tree.pack(fill="both", expand=True)
        return tree

    def _build_io_frame(self, io_frame):
        """
        Two sections in one box: momentary buttons on top (must be physically
        pressed to go PASS - see BUTTON_NAMES comment), a gap, then everything
        else (faults, driven outputs, LEDs) below using the normal
        compare-to-expected PASS/FAIL.
        """
        press_section = ttk.Frame(io_frame)
        press_section.pack(fill="x")
        ttk.Label(press_section, text="Press to test:", font=("TkDefaultFont", 9, "bold")).pack(anchor="w")

        self.button_confirmed = set()
        self.button_row_widgets = {}
        for name in BUTTON_NAMES:
            row = ttk.Frame(press_section)
            row.pack(fill="x", pady=1)
            check_lbl = ttk.Label(row, text="☐", width=2)  # unchecked box
            check_lbl.pack(side="left")
            ttk.Label(row, text=name, width=16, anchor="w").pack(side="left")
            status_lbl = ttk.Label(row, text="", foreground="#1a8a1a", font=("TkDefaultFont", 9, "bold"))
            status_lbl.pack(side="left")
            self.button_row_widgets[name] = (check_lbl, status_lbl)

        ttk.Separator(io_frame, orient="horizontal").pack(fill="x", pady=6)

        self.io_tree = self._make_result_tree(io_frame, ("param", "result"),
                                                (("param", "Parameter", 150), ("result", "Result", 70)))

    def _update_button_press_row(self, name, raw_value):
        if raw_value == "0":  # active-low: 0 = actually pressed right now
            self.button_confirmed.add(name)
        if name in self.button_confirmed:
            check_lbl, status_lbl = self.button_row_widgets[name]
            check_lbl.config(text="☑")  # checked box
            status_lbl.config(text="PASS")

    def _build_relay_row(self, param_frame):
        """
        Live toggle for the DC bus contactor relay (Cont_Enable/GPIO14),
        independent of running a full self-test - lets you watch VBus_AD/
        DC_BUS respond to open vs closed without needing to click Start Test.
        Note: running Start Test still force-closes this same relay via the
        Cont_Enable test entry, so it'll snap back to closed on the next run
        regardless of this toggle's state.
        """
        row = ttk.Frame(param_frame)
        row.pack(fill="x", pady=(0, 6))

        self.relay_closed_var = tk.BooleanVar(value=False)
        self.relay_toggle_btn = ttk.Button(
            row, text="Relay: OPEN", command=self._on_relay_toggle, state="disabled"
        )
        self.relay_toggle_btn.pack(side="left")

    def _build_fan_panel(self, fan_frame):
        self.fan_status_var = tk.StringVar(value="FAIL")
        self.fan_status_label = tk.Label(
            fan_frame, textvariable=self.fan_status_var, font=("TkDefaultFont", 12, "bold"),
            fg="#d32f2f",
        )
        self.fan_status_label.pack(anchor="w")

        self.fan_on_var = tk.BooleanVar(value=False)
        self.fan_toggle_btn = ttk.Button(
            fan_frame, text="Fan OFF", command=self._on_fan_toggle, state="disabled"
        )
        self.fan_toggle_btn.pack(anchor="w", pady=(8, 0))

        speed_row = ttk.Frame(fan_frame)
        speed_row.pack(fill="x", pady=(8, 0))
        ttk.Label(speed_row, text="Speed:").pack(side="left")
        self.fan_speed_var = tk.IntVar(value=50)
        self.fan_speed_scale = ttk.Scale(
            speed_row, from_=0, to=100, orient="horizontal",
            variable=self.fan_speed_var, command=self._on_fan_speed_change,
            state="disabled",
        )
        self.fan_speed_scale.pack(side="left", fill="x", expand=True, padx=(8, 8))
        self.fan_speed_label = ttk.Label(speed_row, text="50%", width=5)
        self.fan_speed_label.pack(side="left")

    # ---- Port / connection handling ---------------------------------------

    def _refresh_ports(self):
        ports = list_ports()
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _on_connect(self):
        if self.client.is_connected():
            self.client.disconnect()
            self.connect_btn.config(text="Connect")
            self.start_btn.config(state="disabled")
            self.stop_btn.config(state="disabled")
            self.fan_toggle_btn.config(state="disabled")
            self.fan_speed_scale.config(state="disabled")
            self.relay_toggle_btn.config(state="disabled")
            self.debug_entry.config(state="disabled")
            self.debug_send_btn.config(state="disabled")
            self.status_var.set("Not connected")
            self._set_status_color("#cccccc")
            return

        port = self.port_var.get()
        if not port and not isinstance(self.client, MockTestClient):
            messagebox.showerror("No port selected", "Select a serial port first.")
            return

        try:
            self.client.connect(port, baud=DEFAULT_BAUD)
        except Exception as exc:  # pyserial raises various OSError/SerialException subtypes
            messagebox.showerror("Connection failed", str(exc))
            return

        self.connect_btn.config(text="Disconnect")
        self.start_btn.config(state="normal")
        self.fan_toggle_btn.config(state="normal")
        self.fan_speed_scale.config(state="normal")
        self.relay_toggle_btn.config(state="normal")
        self.debug_entry.config(state="normal")
        self.debug_send_btn.config(state="normal")
        self.status_var.set("Connected - waiting for board")
        self._set_status_color("#e6e6e6")

        # Fresh board -> fresh "has this button actually been pressed" state.
        self.button_confirmed.clear()
        for check_lbl, status_lbl in self.button_row_widgets.values():
            check_lbl.config(text="☐")
            status_lbl.config(text="")

        # Fresh board -> relay is OPEN at boot (RelayControl_Init() default).
        self.relay_closed_var.set(False)
        self.relay_toggle_btn.config(text="Relay: OPEN")

    # ---- Test run -----------------------------------------------------

    def _on_start_test(self):
        if not self.client.is_connected():
            # Can be reached via Live Mode's auto-repeat (self.after callback)
            # firing after the board disconnected mid-session (USB unplugged,
            # serial error, etc.) - the "disconnected" event already reset
            # button states, so just stop the auto-repeat loop instead of
            # calling send() on a dead connection (which raises).
            self.live_mode_var.set(False)
            self.stop_btn.config(state="disabled")
            return
        self.current_run_results = []
        self.expected_count = None
        if not self.live_mode_var.get():
            self.status_var.set("Running...")
            self._set_status_color("#f5f0a9")
        self.start_btn.config(state="disabled")
        self.stop_btn.config(state="normal")
        self.client.start_test()

    def _on_stop_test(self):
        """
        Stops Live Mode's auto-repeat (prevents the next cycle from being
        scheduled). Can't abort a cycle already in flight - SelfTest_RunAll()
        on the firmware side runs to completion once START is sent.
        """
        self.live_mode_var.set(False)
        self.stop_btn.config(state="disabled")

    # ---- Relay control -----------------------------------------------------

    def _on_relay_toggle(self):
        closing = not self.relay_closed_var.get()
        self.relay_closed_var.set(closing)
        self.relay_toggle_btn.config(text="Relay: CLOSED" if closing else "Relay: OPEN")
        if self.client.is_connected():
            self.client.set_relay(closing)
            # Relay-only commands don't produce new TEST, lines on their own -
            # without this, VBus_AD/DC_BUS (and everything else) would keep
            # showing whatever they were from the last full test run instead
            # of the real, current post-toggle state. Kick off a fresh run so
            # the panels actually reflect what just changed - matches what a
            # multimeter at the board would show right now, not stale data.
            if not self.live_mode_var.get():
                self._on_start_test()

    # ---- Fan control -----------------------------------------------------

    def _on_fan_toggle(self):
        turning_on = not self.fan_on_var.get()
        self.fan_on_var.set(turning_on)
        self.fan_toggle_btn.config(text="Fan ON" if turning_on else "Fan OFF")
        percent = self.fan_speed_var.get() if turning_on else 0
        self.fan_ack_seen = False
        self._update_fan_status()
        if self.client.is_connected():
            self.client.set_fan_speed(percent)

    def _on_fan_speed_change(self, _value):
        percent = int(self.fan_speed_var.get())
        self.fan_speed_label.config(text=f"{percent}%")
        if not (self.fan_on_var.get() and self.client.is_connected()):
            return
        # Debounce: a Scale drag fires this continuously (once per pixel).
        # Sending FAN_SET on every single one of those can flood the UART
        # faster than the firmware's blocking command loop drains it. Coalesce
        # into one send ~120ms after the drag settles - this is the concrete
        # fix for "fan speed not working": previously every drag pixel sent
        # its own command, and only the LAST one to actually get processed
        # mattered, so fast drags could visually "stick" or lag badly.
        if self._fan_slider_after_id is not None:
            self.after_cancel(self._fan_slider_after_id)
        self._fan_slider_after_id = self.after(FAN_SLIDER_DEBOUNCE_MS, self._send_fan_speed_now, percent)

    def _send_fan_speed_now(self, percent):
        self._fan_slider_after_id = None
        if self.client.is_connected():
            self.client.set_fan_speed(percent)

    def _update_fan_status(self):
        """PASS = commanded on AND board acknowledged a non-zero duty cycle.
        This confirms the command was received, not a real tachometer/RPM
        reading (Fan_Spd_FB isn't wired into this GUI yet) - closest available
        proxy for "fan is on/running" without that feedback signal."""
        running = self.fan_on_var.get() and self.fan_ack_seen and self.fan_speed_var.get() > 0
        self.fan_status_var.set("PASS" if running else "FAIL")
        self.fan_status_label.config(fg="#1a8a1a" if running else "#d32f2f")

    # ---- Event pump -----------------------------------------------------

    def _poll_events(self):
        try:
            while True:
                event = self.client.events.get_nowait()
                self._handle_event(event)
        except queue.Empty:
            pass
        self.after(POLL_INTERVAL_MS, self._poll_events)

    def _handle_event(self, event):
        etype = event["type"]

        if etype == "ready":
            self.status_var.set("Board ready")
            self._set_status_color("#e6e6e6")

        elif etype == "begin":
            self.expected_count = event["count"]

        elif etype == "test_result":
            self.current_run_results.append(event)
            self._route_test_result(event)

        elif etype == "overall_result":
            passed = event["result"] == "PASS"
            self.status_var.set(f"OVERALL: {event['result']}")
            self._set_status_color("#4caf50" if passed else "#e53935", fg="white")
            self.start_btn.config(state="normal")
            self._write_log(event["result"])
            if self.live_mode_var.get() and self.client.is_connected():
                self.after(LIVE_MODE_REPEAT_DELAY_MS, self._on_start_test)
            else:
                self.stop_btn.config(state="disabled")

        elif etype == "fan_ack":
            self.fan_ack_seen = True
            self.fan_speed_label.config(text=f"{event['percent']}%")
            self._update_fan_status()

        elif etype == "relay_ack":
            # Board's actual applied state, not just what we optimistically set
            # on click - keeps the button label truthful if something else
            # (e.g. a Start Test run's Cont_Enable entry) changed it meanwhile.
            closed = bool(event["state"])
            self.relay_closed_var.set(closed)
            self.relay_toggle_btn.config(text="Relay: CLOSED" if closed else "Relay: OPEN")

        elif etype == "disconnected":
            self.status_var.set("Board disconnected")
            self._set_status_color("#cccccc")
            self.connect_btn.config(text="Connect")
            self.start_btn.config(state="disabled")
            self.stop_btn.config(state="disabled")
            self.fan_toggle_btn.config(state="disabled")
            self.fan_speed_scale.config(state="disabled")
            self.relay_toggle_btn.config(state="disabled")
            self.debug_entry.config(state="disabled")
            self.debug_send_btn.config(state="disabled")

        elif etype == "raw":
            # Unrecognized line, e.g. COIL_STATUS/BUTTON_STATUS replies -
            # shown in the Debug Console log, not just printed to the
            # terminal (which nothing in the GUI window used to surface).
            self._append_debug_log(event["line"])

    def _route_test_result(self, event):
        name = event["name"]
        result = event["result"]
        value_text = format_value(name, event["test_type"], event["value"])

        if name in BUTTON_NAMES:
            self._update_button_press_row(name, event["value"])
        elif name in TEMP_AD_NAMES:
            self._upsert_row(self.temp_tree, self.temp_row_items, name,
                              (display_name(name), value_text, result), result)
        elif name in PARAM_NAMES:
            self._upsert_row(self.param_tree, self.param_row_items, name,
                              (display_name(name), value_text, result), result)
            if name == "VBus_AD":
                self._insert_dc_bus_row(event["value"])
        else:
            self._upsert_row(self.io_tree, self.io_row_items, name,
                              (display_name(name), result), result)

    def _upsert_row(self, tree, row_items, key, values, result_tag):
        existing = row_items.get(key)
        if existing is not None and tree.exists(existing):
            tree.item(existing, values=values, tags=(result_tag,))
        else:
            item_id = tree.insert("", "end", values=values, tags=(result_tag,))
            row_items[key] = item_id

    def _insert_dc_bus_row(self, vbus_raw_value):
        """
        GUI-derived row (not a separate firmware test) - VBus_AD's own formula
        already reverses the divider+gain back to the original DC_BUS scale, so
        this just re-labels that same computed voltage as a plain 1/0 "is the
        bus energized" indicator.
        """
        result = dc_bus_row(vbus_raw_value)
        if result is None:
            return
        present, volts = result
        self._upsert_row(self.param_tree, self.param_row_items, "DC_BUS",
                          ("DC_BUS", f"{present} ({volts:.1f}V)", "INFO"), "INFO")

    def _set_status_color(self, bg, fg="black"):
        self.status_label.config(bg=bg, fg=fg)

    # ---- Logging -----------------------------------------------------

    def _write_log(self, overall_result):
        os.makedirs(LOG_DIR, exist_ok=True)
        timestamp = datetime.datetime.now()
        filename = os.path.join(LOG_DIR, timestamp.strftime("%Y%m%d_%H%M%S") + ".csv")

        with open(filename, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["timestamp", timestamp.isoformat()])
            writer.writerow(["overall_result", overall_result])
            writer.writerow([])
            writer.writerow(["name", "type", "result", "value"])
            for r in self.current_run_results:
                writer.writerow([r["name"], r["test_type"], r["result"], r["value"]])


def main():
    parser = argparse.ArgumentParser(description="Board self-test GUI")
    parser.add_argument("--mock", action="store_true", help="Use a fake board, no hardware needed")
    args = parser.parse_args()

    client = MockTestClient() if args.mock else SerialTestClient()
    app = SelfTestApp(client)
    app.mainloop()


if __name__ == "__main__":
    main()
