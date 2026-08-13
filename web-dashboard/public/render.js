// Shared rendering code - used by both index.html (polls /api/latest, someone
// else's board) and connect.html (reads the board directly via Web Serial in
// this same tab). Kept in one file so the two pages can never drift apart
// visually - if you change how a row looks, it changes everywhere.

function rowsFor(rows, panel) {
  return rows.filter(r => r.panel === panel);
}

function renderResultTable(tbodyId, rows) {
  const tbody = document.querySelector(`#${tbodyId} tbody`);
  if (rows.length === 0) {
    tbody.innerHTML = '<tr><td class="empty">No data yet</td></tr>';
    return;
  }
  tbody.innerHTML = rows.map(r => `
    <tr>
      <td>${r.display_name}</td>
      <td>${r.value ?? ''}</td>
      <td class="${r.result}">${r.result}</td>
    </tr>
  `).join('');
}

function renderButtons(rows) {
  const el = document.getElementById('buttons');
  if (rows.length === 0) {
    el.innerHTML = '<div class="empty">No data yet</div>';
    return;
  }
  el.innerHTML = rows.map(r => `
    <div class="btn-row">
      <span class="box">${r.confirmed ? '☑' : '☐'}</span>
      <span>${r.display_name}</span>
      <span class="PASS">${r.confirmed ? 'PASS' : ''}</span>
    </div>
  `).join('');
}

// snap = { overall, timestamp, rows: [...] } - same shape relay.py posts and
// connect.html builds locally.
function renderSnapshot(snap) {
  const meta = document.getElementById('meta');
  const banner = document.getElementById('banner');

  if (!snap || !snap.timestamp) {
    meta.textContent = 'Waiting for first update...';
  } else {
    const ago = Math.max(0, Math.round(Date.now() / 1000 - snap.timestamp));
    meta.textContent = `Last update: ${ago}s ago`;
  }

  const rows = (snap && snap.rows) || [];
  renderButtons(rowsFor(rows, 'buttons'));
  renderResultTable('temp-table', rowsFor(rows, 'temperature'));
  renderResultTable('param-table', rowsFor(rows, 'param'));
  renderResultTable('io-table', rowsFor(rows, 'io'));

  const overall = snap && snap.overall;
  banner.className = overall || '';
  banner.textContent = overall ? `OVERALL: ${overall}` : 'No data yet';
}
