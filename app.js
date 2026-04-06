/* =====================================================
   MealMax — Frontend JS
   Communicates with C backend at localhost:8080
   ===================================================== */

const API_BASE = 'http://localhost:8080';

/* =====================
   SERVER STATUS CHECK
   ===================== */
async function checkServer() {
  try {
    const r = await fetch(`${API_BASE}/api/dishes`, { method: 'GET' });
    if (r.ok) {
      document.getElementById('statusDot').className  = 'status-dot online';
      document.getElementById('statusText').textContent = 'C Server Online';
      return true;
    }
  } catch(e) {}
  document.getElementById('statusDot').className  = 'status-dot offline';
  document.getElementById('statusText').textContent = 'Server Offline';
  return false;
}

/* =====================
   STATE
   ===================== */
let state = {
  currentDish: null,
  activePlatform: null,
  platformData: null,
  coupons: [],
  lastResult: null,
};

/* =====================
   TRIE AUTOCOMPLETE
   ===================== */
const searchInput = document.getElementById('searchInput');
const autocompleteBox = document.getElementById('autocomplete');
const clearBtn = document.getElementById('clearBtn');
const trieTag  = document.getElementById('trieTag');

if (searchInput) {
  let debounceTimer;
  searchInput.addEventListener('input', () => {
    clearTimeout(debounceTimer);
    const val = searchInput.value.trim();
    clearBtn?.classList.toggle('show', val.length > 0);
    if (val.length < 1) { hideAutocomplete(); return; }
    debounceTimer = setTimeout(() => fetchAutocomplete(val), 160);
  });

  searchInput.addEventListener('keydown', e => {
    if (e.key === 'Escape') hideAutocomplete();
  });

  document.addEventListener('click', e => {
    if (!e.target.closest('.search-block')) hideAutocomplete();
  });
}

async function fetchAutocomplete(prefix) {
  try {
    const r = await fetch(`${API_BASE}/api/search?q=${encodeURIComponent(prefix)}`);
    const d = await r.json();
    if (d.results && d.results.length > 0) {
      renderAutocomplete(d.results, prefix, d);
    } else {
      hideAutocomplete();
    }
  } catch(e) {
    hideAutocomplete();
  }
}

function renderAutocomplete(results, prefix, meta) {
  if (!autocompleteBox) return;
  autocompleteBox.innerHTML = results.slice(0, 7).map(w => {
    const idx = w.toLowerCase().indexOf(prefix.toLowerCase());
    const highlighted = idx >= 0
      ? w.slice(0, idx) +
        `<span class="ac-match">${w.slice(idx, idx + prefix.length)}</span>` +
        w.slice(idx + prefix.length)
      : w;
    return `<div class="ac-item" onclick="selectDish('${w}')">
      🍽️ ${highlighted}
      <span class="ac-sub">Trie</span>
    </div>`;
  }).join('');
  autocompleteBox.classList.add('show');
  if (trieTag) {
    trieTag.style.display = 'block';
    trieTag.textContent = `⬆ Trie autocomplete — ${meta.complexity} — ${meta.count} match(es)`;
  }
}

function hideAutocomplete() {
  autocompleteBox?.classList.remove('show');
}

function clearSearch() {
  if (searchInput) searchInput.value = '';
  hideAutocomplete();
  clearBtn?.classList.remove('show');
  if (trieTag) trieTag.style.display = 'none';
}

/* =====================
   DISH SELECTION (HashMap lookup)
   ===================== */
async function selectDish(name) {
  if (searchInput) searchInput.value = name;
  clearBtn?.classList.add('show');
  hideAutocomplete();

  state.currentDish = name;
  state.activePlatform = null;

  try {
    // HashMap O(1) lookup via C backend
    const r = await fetch(`${API_BASE}/api/prices?dish=${encodeURIComponent(name)}`);
    const d = await r.json();
    if (d.error) { showToast('Dish not found'); return; }

    state.platformData = d;
    renderPlatforms(d);

    // Default to cheapest platform
    const cheapest = d.platforms.reduce((a, b) => a.total < b.total ? a : b);
    state.activePlatform = cheapest.platform;
    highlightPlatform(cheapest.platform, d.platforms);

    // Load coupons
    await loadCoupons(name, cheapest.platform);

    showToast(`"${name}" loaded — HashMap O(1) · bucket #${d.hash_bucket}`);
  } catch(e) {
    showToast('Cannot reach C backend — is server running?');
  }
}

function renderPlatforms(d) {
  const platforms = d.platforms;
  const cheapest = platforms.reduce((a, b) => a.total < b.total ? a : b);

  platforms.forEach(p => {
    const key = p.platform.toLowerCase();
    const priceEl  = document.getElementById(`price${p.platform}`);
    const detailEl = document.getElementById(`detail${p.platform}`);
    const crownEl  = document.getElementById(`crown${p.platform}`);
    if (priceEl)  { priceEl.textContent  = `₹${p.total.toFixed(0)}`; }
    if (detailEl) { detailEl.textContent = `₹${p.base} + ₹${p.delivery} del + ₹${p.tax.toFixed(0)} tax`; }
    if (priceEl)  priceEl.classList.toggle('best', p.platform === cheapest.platform);
    if (crownEl)  crownEl.style.display = p.platform === cheapest.platform ? 'block' : 'none';
  });
}

function highlightPlatform(platform, platforms) {
  document.querySelectorAll('.platform-card').forEach(c => {
    c.classList.toggle('selected', c.dataset.platform === platform);
  });
  state.activePlatform = platform;
}

async function selectPlatform(platform) {
  if (!state.currentDish) return;
  if (state.platformData) {
    highlightPlatform(platform, state.platformData.platforms);
  }
  await loadCoupons(state.currentDish, platform);
}

/* =====================
   COUPONS
   ===================== */
async function loadCoupons(dish, platform) {
  const area = document.getElementById('couponsArea');
  if (!area) return;
  area.innerHTML = `<p class="muted-hint">Loading coupons…</p>`;

  try {
    const r = await fetch(
      `${API_BASE}/api/coupons?dish=${encodeURIComponent(dish)}&platform=${encodeURIComponent(platform)}`
    );
    const d = await r.json();
    state.coupons = d.coupons || [];
    renderCoupons(d.coupons || [], d.subtotal, platform);
  } catch(e) {
    area.innerHTML = `<p class="muted-hint">Could not load coupons</p>`;
  }
}

function renderCoupons(coupons, subtotal, platform) {
  const area = document.getElementById('couponsArea');
  if (!area) return;

  if (coupons.length === 0) {
    area.innerHTML = `<p class="muted-hint">No coupons applicable for ${platform} at ₹${subtotal?.toFixed(0)}</p>`;
    return;
  }

  // Greedy: find best single coupon
  const greedyBest = coupons.reduce((a, b) => a.saving > b.saving ? a : b, coupons[0]);

  area.innerHTML = `
    <div class="coupon-list">
      ${coupons.map((c, i) => `
        <div class="coupon-item" id="coupon_${i}" onclick="toggleCoupon(${i})">
          <div class="coupon-check" id="check_${i}">✓</div>
          <div class="coupon-body">
            <div class="coupon-code">${c.code}${c.code === greedyBest.code ? ' <span style="font-size:0.6rem;color:var(--green)">← greedy pick</span>' : ''}</div>
            <div class="coupon-desc">${c.desc}</div>
          </div>
          <div class="coupon-save">-₹${c.saving.toFixed(0)}</div>
        </div>
      `).join('')}
    </div>
    <div class="greedy-tag">
      🟢 Greedy O(n): picks <strong>${greedyBest.code}</strong> (saves ₹${greedyBest.saving.toFixed(0)}) &nbsp;·&nbsp;
      🟡 DP Knapsack finds optimal combo on "Find Best Deal"
    </div>
  `;
}

function toggleCoupon(idx) {
  const card  = document.getElementById(`coupon_${idx}`);
  const check = document.getElementById(`check_${idx}`);
  if (!card) return;
  card.classList.toggle('active');
}

/* =====================
   OPTIMIZE (All 5 DSA)
   ===================== */
async function runOptimize() {
  if (!state.currentDish) {
    showToast('⚠️ Search for a dish first!'); return;
  }

  const btn = document.getElementById('optimizeBtn');
  if (btn) { btn.disabled = true; btn.querySelector('span').textContent = '⏳ Running algorithms…'; }

  try {
    const r = await fetch(
      `${API_BASE}/api/optimize?dish=${encodeURIComponent(state.currentDish)}`
    );
    const d = await r.json();
    if (d.error) { showToast(d.error); return; }

    state.lastResult = d;

    // Save for DSA visualizer page
    try { localStorage.setItem('mealmax_last_result', JSON.stringify(d)); } catch(e) {}

    renderResults(d);
    showToast(`✅ Best deal found — ${d.rankings[0]?.platform} @ ₹${d.rankings[0]?.final_price.toFixed(0)}`);
  } catch(e) {
    showToast('Cannot reach C backend — is server running?');
  } finally {
    if (btn) { btn.disabled = false; btn.querySelector('span').textContent = '🚀 Find Best Deal'; }
  }
}

function renderResults(d) {
  const emptyEl   = document.getElementById('emptyState');
  const resultsEl = document.getElementById('resultsArea');
  if (emptyEl)   emptyEl.style.display   = 'none';
  if (resultsEl) resultsEl.style.display = 'block';

  const best = d.rankings[0];
  const worst = Math.max(...d.rankings.map(r => r.base + r.delivery + r.tax));

  // Winner
  const p2emoji = { Swiggy: '🧡', Zomato: '❤️', Magicpin: '💜' };
  set('winnerPlatform', `${p2emoji[best.platform] || ''} ${best.platform}`);
  set('winnerPrice',    `₹${best.final_price.toFixed(0)}`);
  set('winnerCoupon',   best.coupons_used
    ? `Applied: ${best.coupons_used}` : 'No coupon applied');
  set('winnerSave',     `You save ₹${(worst - best.final_price).toFixed(0)} vs worst deal`);

  // Breakdown
  const bdEl = document.getElementById('breakdownGrid');
  if (bdEl) bdEl.innerHTML = `
    <div class="bd-row"><span class="bd-label">Base Price</span>
      <span class="bd-val">₹${best.base.toFixed(0)}</span></div>
    <div class="bd-row"><span class="bd-label">Delivery</span>
      <span class="bd-val">₹${best.delivery.toFixed(0)}</span></div>
    <div class="bd-row"><span class="bd-label">GST (5%)</span>
      <span class="bd-val">₹${best.tax.toFixed(0)}</span></div>
    <div class="bd-row"><span class="bd-label">Coupon Discount</span>
      <span class="bd-val accent">-₹${best.saving.toFixed(0)}</span></div>
    <div class="bd-row"><span style="font-weight:600">Total Payable</span>
      <span class="bd-val total">₹${best.final_price.toFixed(0)}</span></div>
    <div class="bd-row"><span class="bd-label">You Save</span>
      <span class="bd-val green">₹${(worst - best.final_price).toFixed(0)}</span></div>
  `;

  // Min-Heap rankings
  const heapEl = document.getElementById('heapList');
  if (heapEl) heapEl.innerHTML = d.rankings.map((r, i) => `
    <div class="heap-rank-item">
      <div class="heap-rank-num">#${i+1}</div>
      <div class="heap-rank-info">
        <div class="heap-rank-name">${p2emoji[r.platform]||''} ${r.platform}</div>
        <div class="heap-rank-detail">${r.coupons_used || 'no coupon'} · saved ₹${r.saving.toFixed(0)}</div>
      </div>
      <div class="heap-rank-price">₹${r.final_price.toFixed(0)}</div>
    </div>
  `).join('');

  // Algo trace
  const t = d.algo_trace;
  const traceEl = document.getElementById('algoTrace');
  if (traceEl && t) {
    const greedyBest = t.greedy.log.reduce((a,b) => a.greedy_saving > b.greedy_saving ? a : b, t.greedy.log[0] || {});
    const dpBest     = t.dp_knapsack.log.reduce((a,b) => a.dp_saving > b.dp_saving ? a : b, t.dp_knapsack.log[0] || {});
    traceEl.innerHTML = `
      <div class="trace-step">
        <div class="trace-icon hashmap">HM</div>
        <div class="trace-body">
          <div class="trace-name hashmap">HashMap — ${t.hashmap.complexity}</div>
          <div class="trace-desc">Looked up "${d.dish}" in O(1). Hash bucket: #${t.hashmap.bucket}. Loaded prices for ${d.rankings.length} platforms.</div>
        </div>
      </div>
      <div class="trace-step">
        <div class="trace-icon trie">TR</div>
        <div class="trace-body">
          <div class="trace-name trie">Trie — ${t.trie.complexity}</div>
          <div class="trace-desc">Prefix traversal during search. Each character reduces search space by branching into child nodes.</div>
        </div>
      </div>
      <div class="trace-step">
        <div class="trace-icon greedy">GR</div>
        <div class="trace-body">
          <div class="trace-name greedy">Greedy — ${t.greedy.complexity}</div>
          <div class="trace-desc">Scanned all coupons once. Best single coupon: ${greedyBest.coupon || 'none'} on ${greedyBest.platform || '—'} (saves ₹${(greedyBest.greedy_saving||0).toFixed(0)}).</div>
        </div>
      </div>
      <div class="trace-step">
        <div class="trace-icon dp">DP</div>
        <div class="trace-body">
          <div class="trace-name dp">DP Knapsack — ${t.dp_knapsack.complexity}</div>
          <div class="trace-desc">Built full DP table for optimal coupon combination. Best combo on ${dpBest.platform||'—'} saves ₹${(dpBest.dp_saving||0).toFixed(0)}. Coupons: ${dpBest.dp_coupons||'none'}.</div>
        </div>
      </div>
      <div class="trace-step">
        <div class="trace-icon heap">HP</div>
        <div class="trace-body">
          <div class="trace-name heap">Min-Heap — ${t.min_heap.complexity}</div>
          <div class="trace-desc">Inserted ${t.min_heap.size} deals into min-heap. Extracted minimum in O(log ${t.min_heap.size}). Winner: ${best.platform} @ ₹${best.final_price.toFixed(0)}.</div>
        </div>
      </div>
    `;
  }
}

/* =====================
   UTILS
   ===================== */
function set(id, html) {
  const el = document.getElementById(id);
  if (el) el.innerHTML = html;
}

function showToast(msg) {
  const toast   = document.getElementById('toast');
  const toastMsg = document.getElementById('toastMsg');
  if (!toast || !toastMsg) return;
  toastMsg.textContent = msg;
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 3200);
}

/* =====================
   INIT
   ===================== */
document.addEventListener('DOMContentLoaded', async () => {
  await checkServer();
  setInterval(checkServer, 8000);

  // Pre-load Biryani on main page
  if (document.getElementById('searchInput') &&
      document.getElementById('platformsRow')) {
    setTimeout(() => selectDish('Biryani'), 500);
  }
});
