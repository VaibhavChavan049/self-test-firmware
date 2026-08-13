"""
Headless relay: runs on the same PC as the blue USB-serial adapter, reads the
board exactly like app.py does, and pushes each completed test run to a
Vercel-hosted dashboard so it's viewable from any browser over a link.

This does NOT replace app.py - it's a second, parallel front end for the same
board data. Run app.py locally if you want the full interactive GUI (fan
slider, live per-press button confirmation, etc). Run this instead/alongside
if you want that data visible on a web dashboard.

Why this has to run on this PC and not "in the cloud": the board only talks
over the USB-serial adapter plugged into whatever machine is physically next
to it. Vercel's servers have no path to that USB port - so this script is the
bridge: it does the real serial read locally, then POSTs the result to the
dashboard's API over the internet.

Usage:
    python relay.py --url https://your-app.vercel.app --token YOUR_SECRET
    python relay.py --url ... --token ... --mock   # no hardware needed, fakes a board
    python relay.py --url ... --token ... --port COM5 --interval 2

See web-dashboard/README.md for how to get --url and --token (set the same
token as the RELAY_TOKEN environment variable when you deploy the dashboard).
"""

import argparse
import queue
import sys
import time

import requests

from serial_comm import SerialTestClient, MockTestClient, list_ports, DEFAULT_BAUD
from formatting import (
    TEMP_AD_NAMES,
    PARAM_NAMES,
    BUTTON_NAMES,
    display_name,
    format_value,
    dc_bus_row,
)

DEFAULT_INTERVAL_S = 3.0  # see web-dashboard/README.md "Is this free?" for why not faster
EVENT_TIMEOUT_S = 10.0  # how long to wait for the board mid-run before giving up on that cycle


def panel_for(name):
    if name in BUTTON_NAMES:
        return "buttons"
    if name in TEMP_AD_NAMES:
        return "temperature"
    if name in PARAM_NAMES:
        return "param"
    return "io"


def run_one_cycle(client):
    """
    Sends START, blocks until the board reports RESULT,OVERALL,..., and
    returns a JSON-able snapshot dict. Returns None if the board never
    responded (disconnected / timed out) so the caller can skip posting.
    """
    client.start_test()

    rows = []
    button_confirmed = set()
    overall = None
    deadline = time.monotonic() + EVENT_TIMEOUT_S

    while overall is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            print("[relay] timed out waiting for board mid-run, skipping this cycle", file=sys.stderr)
            return None
        try:
            event = client.events.get(timeout=remaining)
        except queue.Empty:
            print("[relay] timed out waiting for board mid-run, skipping this cycle", file=sys.stderr)
            return None

        etype = event["type"]
        if etype == "disconnected":
            print("[relay] board disconnected", file=sys.stderr)
            return None
        if etype != "test_result" and etype != "overall_result":
            continue  # ignore ready/pong/fan_ack/raw here, same as app.py's routing does per-panel

        if etype == "test_result":
            name = event["name"]
            deadline = time.monotonic() + EVENT_TIMEOUT_S  # saw activity, extend the deadline

            if name in BUTTON_NAMES and event["value"] == "0":
                button_confirmed.add(name)

            row = {
                "name": name,
                "display_name": display_name(name),
                "test_type": event["test_type"],
                "result": event["result"],
                "raw_value": event["value"],
                "value": format_value(name, event["test_type"], event["value"]),
                "panel": panel_for(name),
            }
            if name in BUTTON_NAMES:
                row["confirmed"] = name in button_confirmed
            rows.append(row)

            if name == "VBus_AD":
                dc = dc_bus_row(event["value"])
                if dc is not None:
                    present, volts = dc
                    rows.append({
                        "name": "DC_BUS",
                        "display_name": "DC_BUS",
                        "test_type": "STATUS",
                        "result": "INFO",
                        "raw_value": str(present),
                        "value": f"{present} ({volts:.1f}V)",
                        "panel": "param",
                    })

        elif etype == "overall_result":
            overall = event["result"]

    return {
        "overall": overall,
        "timestamp": time.time(),
        "rows": rows,
    }


def post_snapshot(dashboard_url, token, snapshot):
    try:
        resp = requests.post(
            f"{dashboard_url.rstrip('/')}/api/update",
            json=snapshot,
            headers={"X-Relay-Token": token},
            timeout=5,
        )
        resp.raise_for_status()
    except requests.RequestException as exc:
        print(f"[relay] failed to push to dashboard: {exc}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Push board self-test results to a Vercel dashboard")
    parser.add_argument("--url", required=True, help="Deployed dashboard base URL, e.g. https://your-app.vercel.app")
    parser.add_argument("--token", required=True, help="Shared secret - must match the dashboard's RELAY_TOKEN env var")
    parser.add_argument("--port", help="Serial port (default: first one found)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL_S,
                         help="Seconds to wait between test runs (default: %(default)s)")
    parser.add_argument("--mock", action="store_true", help="Use a fake board, no hardware needed")
    args = parser.parse_args()

    client = MockTestClient() if args.mock else SerialTestClient()

    port = args.port
    if not args.mock and not port:
        ports = list_ports()
        if not ports:
            print("[relay] no serial ports found - is the blue USB adapter plugged in?", file=sys.stderr)
            sys.exit(1)
        port = ports[0]
        print(f"[relay] no --port given, using {port}")

    client.connect(port, baud=args.baud)
    print(f"[relay] connected on {port if not args.mock else '(mock)'}, pushing to {args.url} every {args.interval}s")

    try:
        while True:
            snapshot = run_one_cycle(client)
            if snapshot is not None:
                post_snapshot(args.url, args.token, snapshot)
                print(f"[relay] pushed run - overall={snapshot['overall']}, {len(snapshot['rows'])} rows")
                for row in snapshot["rows"]:
                    if row["name"] == "VBus_AD":
                        print(f"[relay]   VBus_AD raw={row['raw_value']}  value={row['value']}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[relay] stopping")
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
