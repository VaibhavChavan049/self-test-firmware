# Board Self-Test - Web Dashboard

A read-only web view of the same test results `gui-app/app.py` shows, so
anyone with the link can see live PASS/FAIL data without needing the board,
the serial adapter, or Python installed.

## How it actually works

The board only talks over the USB-serial adapter plugged into one specific
PC. Vercel's servers can't reach that adapter - someone/something local has
to be the bridge. Two ways to be that bridge:

- **`gui-app/relay.py`** (Python, any OS) - a terminal script you run and
  leave running on the PC connected to the board.
- **`public/connect.html`** (no install at all) - open this page itself in
  **Chrome or Edge** on the PC connected to the board, click **Connect Board**,
  and the page talks to the USB adapter directly using the browser's
  [Web Serial API](https://developer.chrome.com/docs/capabilities/serial) -
  no Python, no terminal command. Safari and Firefox don't support this API,
  so this only works in Chrome/Edge specifically (any OS - Windows or Mac).

Either way, whichever one is running pushes each completed test run to
`POST /api/update`; anyone's browser looking at `public/index.html` (the
plain dashboard link, works in any browser) polls `GET /api/latest` every
10 seconds to display it. Storage in between is Vercel KV (so the serverless
functions have somewhere to hand data to each other - they don't share
memory between requests).

## Is this free?

Yes, for this project's scale. Two things you're relying on, both checked
directly (not guessed) as of when this was built:

- **Vercel hosting (Hobby plan):** free, no card charged ever without you
  explicitly upgrading to Pro. 100GB bandwidth and 1M function calls/month
  included. If you somehow blow past a Hobby limit, **the project just pauses
  until next month - Vercel does not silently bill you.**
  ([source](https://blog.vibecoder.me/vercel-pricing-explained-when-free-isnt-enough))
- **The database (Upstash, via Vercel's Storage marketplace):** free tier,
  no credit card required to set up - 500,000 commands/month, 256MB storage.
  ([source](https://vercel.com/changelog/upstash-joins-the-vercel-marketplace))

The 10-second dashboard poll and 3-second relay push intervals above were
chosen so normal use (a handful of people with the dashboard open during a
work day) stays well inside that monthly quota. If you later want faster
updates, or expect many people watching at once for full days, check
**Vercel dashboard -> Storage -> your database -> Usage** to see where you
actually stand before making the intervals shorter.

## Deploy steps (do this once)

1. Install the Vercel CLI if you don't have it, and log in:
   ```
   npm i -g vercel
   vercel login
   ```

2. From this folder, link/create the Vercel project:
   ```
   cd web-dashboard
   vercel link
   ```
   (accept the defaults - "Link to existing project?" No, then give it a name)

3. Add a KV database: in the Vercel dashboard, open this project ->
   **Storage** tab -> **Create Database** -> **KV** -> connect it to this
   project. This automatically adds the `KV_REST_API_URL` /
   `KV_REST_API_TOKEN` environment variables the API routes need - you don't
   set those yourself.

4. Add your own secret token so randoms can't post fake data to your
   dashboard: Project -> **Settings** -> **Environment Variables** -> add
   ```
   RELAY_TOKEN = <any random string you make up>
   ```
   (apply it to Production, and Preview if you'll use `vercel dev`/preview
   deploys too). Remember this value - `relay.py` needs the exact same one.

5. Deploy:
   ```
   vercel --prod
   ```
   This prints your live URL, e.g. `https://self-test-dashboard.vercel.app`.
   That's the link to share/open in a browser.

## Running it (every time you want live data on the dashboard)

**Option A - no install, just a browser (Chrome/Edge only):**
On the PC with the board plugged into the blue USB adapter, open
`https://self-test-dashboard.vercel.app/connect.html`, paste in the
`RELAY_TOKEN` you set above, click **Connect Board (USB)**, and pick the
adapter's port from the browser's device picker. That tab now both shows
live results itself *and* publishes them so `index.html` (any browser,
anyone) shows the same thing. Leave that tab open - closing it stops the
board connection, same as stopping `relay.py` would.

**Option B - `relay.py` (any OS, needs Python):**
```
cd gui-app
pip install -r requirements.txt
python relay.py --url https://self-test-dashboard.vercel.app --token <the RELAY_TOKEN you set> 
```
Leave that running.

Either way: open the Vercel URL (`index.html`) in any browser (this PC, your
phone, another PC on a different network - doesn't matter, it's the
internet) - it updates within about 10 seconds as long as one of the two
above keeps running.

To test the dashboard itself without the real board:
```
python relay.py --url https://self-test-dashboard.vercel.app --token <token> --mock
```

## What's NOT on the web dashboard (yet)

Fan control (the ON/OFF button + speed slider) is interactive in `app.py`
and stays local-only for now - making that work over the web means the
dashboard would need a way to send commands *back* to `relay.py`, which is
a bigger change (a command queue the relay polls). The dashboard currently
just shows a placeholder note in the Fan panel. Everything else (Temperature
Test, Param Test, I/O Test incl. the button press-confirmation checkboxes)
is fully live.
