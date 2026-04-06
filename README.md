# MealMax — Smart Food Deal Optimizer
### DSA Project | C Backend + HTML Frontend

---

## Project Overview

**MealMax** compares food prices across Swiggy, Zomato, and Magicpin and finds the cheapest final cost after applying the best coupons — using **5 real DSA algorithms** implemented from scratch in C.

---

## Project Structure

```
mealmax/
├── backend/
│   ├── server.c      ← C HTTP server with all DSA implementations
│   └── Makefile      ← Build script
└── frontend/
    ├── index.html    ← Main optimizer page
    ├── dsa.html      ← Interactive DSA visualizer
    ├── docs.html     ← API documentation (live try-it)
    ├── style.css     ← Shared stylesheet
    └── app.js        ← Frontend JavaScript
```

---

## How to Run

### Step 1: Build & Start the C Backend
```bash
cd backend
make          # compiles server.c → ./server
./server      # starts HTTP server on port 8080
```

You should see:
```
MealMax C Backend running on http://localhost:8080
Loaded 15 dishes, 6 coupons
DSA: HashMap + Trie + Greedy + DP-Knapsack + MinHeap
```

### Step 2: Open the Frontend
Open `frontend/index.html` directly in your browser.  
(No web server needed — it talks to the C backend via fetch)

---

## DSA Algorithms Implemented

### 1. HashMap — `O(1)` dish lookup
```c
// djb2 hash function
unsigned int hash_fn(const char *key) {
    unsigned int h = 5381;
    while (*key) h = ((h << 5) + h) ^ (unsigned char)*key++;
    return h % HASHMAP_SIZE;   // HASHMAP_SIZE = 128
}
```
- **Data**: `HashMap` struct with 128 buckets, chaining for collisions
- **Use**: `GET /api/prices?dish=Biryani` → O(1) average lookup
- **File**: `server.c` lines ~60–100

---

### 2. Trie (Prefix Tree) — `O(m)` autocomplete
```c
typedef struct TrieNode {
    struct TrieNode *children[27];  // a-z + space
    int is_end;
    char word[64];
} TrieNode;
```
- **Data**: 27-way Trie (lowercase a–z + space)
- **Insert**: O(m) where m = word length
- **Search**: O(m) to reach prefix node + DFS to collect all matches
- **Use**: `GET /api/search?q=but` → returns ["Butter Chicken"]
- **File**: `server.c` lines ~103–155

---

### 3. Greedy Algorithm — `O(n)` best coupon
```c
// Scan all coupons once, track best saving
GreedyResult greedy_best_coupon(Coupon *coupons, int n,
                                 const char *platform, double total) {
    GreedyResult best = { NULL, 0.0 };
    for (int i = 0; i < n; i++) {
        // ... compute saving, update best if saving > best.saving
    }
    return best;
}
```
- **Strategy**: At each step, pick the locally optimal choice (max discount)
- **Limitation**: Doesn't find best *combination* of coupons → DP solves this
- **Use**: Highlights the best single coupon on the UI
- **File**: `server.c` lines ~158–185

---

### 4. Dynamic Programming — `O(n×W)` optimal coupon combo
```c
// 0/1 Knapsack
// Items = coupons, weight = coupon.weight, value = discount amount
// Capacity W = 200 (simulated budget constraint)
double dp[n+1][W+1];
for (int i = 1; i <= n; i++) {
    for (int w = 0; w <= W; w++) {
        dp[i][w] = dp[i-1][w];   // don't take coupon i
        if (coupon[i].weight <= w)
            dp[i][w] = max(dp[i][w],
                dp[i-1][w - coupon[i].weight] + saving[i]);  // take it
    }
}
```
- **Time**: O(n × W) where n = number of coupons, W = 200
- **Space**: O(n × W)
- **Traceback**: Recovers which coupons were selected
- **Use**: Finds optimal set of coupons to maximize total discount
- **File**: `server.c` lines ~188–260

---

### 5. Min-Heap (Priority Queue) — `O(log n)` extract minimum
```c
typedef struct { Deal data[MAX_PLATFORMS * 2]; int size; } MinHeap;

void heap_insert(MinHeap *h, Deal d) {
    h->data[h->size++] = d;
    heap_bubble_up(h, h->size - 1);   // O(log n)
}

Deal heap_extract_min(MinHeap *h) {
    Deal min = h->data[0];
    h->data[0] = h->data[--h->size];
    heap_sink_down(h, 0);              // O(log n)
    return min;
}
```
- **Insert**: O(log n)
- **Extract-Min**: O(log n)
- **Use**: After computing final price per platform, heap ranks them cheapest-first
- **File**: `server.c` lines ~263–316

---

## API Endpoints (C Backend)

| Method | Endpoint | DSA Used | Description |
|--------|----------|----------|-------------|
| GET | `/api/dishes` | HashMap | List all dishes |
| GET | `/api/search?q=prefix` | Trie | Autocomplete |
| GET | `/api/prices?dish=Name` | HashMap | Price lookup |
| GET | `/api/coupons?dish=X&platform=Y` | Greedy | Applicable coupons |
| GET | `/api/optimize?dish=Name` | All 5 | Full optimization |

---

## Frontend Pages

| Page | File | Description |
|------|------|-------------|
| Optimizer | `index.html` | Main app — search, compare, optimize |
| DSA Visualizer | `dsa.html` | Interactive visualization of all algorithms |
| API Docs | `docs.html` | Live API reference with try-it buttons |

---

## Example API Calls

```bash
# Autocomplete (Trie)
curl "http://localhost:8080/api/search?q=bi"

# Price lookup (HashMap)
curl "http://localhost:8080/api/prices?dish=Biryani"

# Full optimization (All 5 DSA)
curl "http://localhost:8080/api/optimize?dish=Pizza"

# Coupons for a platform (Greedy)
curl "http://localhost:8080/api/coupons?dish=Pizza&platform=Swiggy"
```

---

## Algorithm Complexity Summary

| Algorithm | Time | Space | Used For |
|-----------|------|-------|----------|
| HashMap | O(1) avg | O(n) | Dish price lookup |
| Trie | O(m) | O(n×α) | Autocomplete search |
| Greedy | O(n) | O(1) | Best single coupon |
| DP Knapsack | O(n×W) | O(n×W) | Optimal coupon combo |
| Min-Heap | O(n log n) | O(n) | Rank cheapest deals |

---

*Built with C (backend) + HTML/CSS/JS (frontend) · No external libraries used*
