// Relay -> dashboard. gui-app/relay.py POSTs one JSON snapshot here after every
// completed test run. Requires a matching X-Relay-Token header so randoms on
// the internet can't overwrite the dashboard - set RELAY_TOKEN in the Vercel
// project's environment variables (see ../README.md) and pass the same value
// to relay.py's --token.
const { kv } = require('@vercel/kv');

const SNAPSHOT_KEY = 'latest_snapshot';

module.exports = async (req, res) => {
  if (req.method !== 'POST') {
    res.status(405).json({ error: 'POST only' });
    return;
  }

  const token = req.headers['x-relay-token'];
  if (!process.env.RELAY_TOKEN || token !== process.env.RELAY_TOKEN) {
    res.status(401).json({ error: 'missing/invalid X-Relay-Token' });
    return;
  }

  const snapshot = req.body;
  if (!snapshot || !Array.isArray(snapshot.rows) || typeof snapshot.overall !== 'string') {
    res.status(400).json({ error: 'expected {overall, timestamp, rows: [...]}' });
    return;
  }

  await kv.set(SNAPSHOT_KEY, snapshot);
  res.status(200).json({ ok: true });
};
