// Dashboard page polls this every second to get the most recent snapshot
// relay.py pushed via /api/update. No auth needed here - read-only, and
// nothing in a snapshot is sensitive (test names/PASS-FAIL/ADC counts).
const { kv } = require('@vercel/kv');

const SNAPSHOT_KEY = 'latest_snapshot';

module.exports = async (req, res) => {
  const snapshot = await kv.get(SNAPSHOT_KEY);
  res.status(200).json(snapshot || { overall: null, timestamp: null, rows: [] });
};
