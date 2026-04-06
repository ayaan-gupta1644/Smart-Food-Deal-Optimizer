/*
 * MealMax - Smart Food Deal Optimizer
 * Backend Server in C
 *
 * DSA Implementations:
 *   1. HashMap     - O(1) dish price lookup
 *   2. Trie        - O(m) prefix autocomplete
 *   3. Greedy      - Best single coupon selection
 *   4. DP Knapsack - Optimal coupon combination
 *   5. Min-Heap    - Rank platform deals cheapest-first
 *
 * HTTP Server: serves JSON API on port 8080
 * Endpoints:
 *   GET  /api/search?q=prefix       -> Trie autocomplete
 *   GET  /api/prices?dish=Name      -> HashMap lookup
 *   POST /api/optimize              -> Greedy + DP + Heap
 *   GET  /api/coupons?dish=Name&platform=X -> applicable coupons
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>

#define PORT          8080
#define BUF_SIZE      8192
#define MAX_DISHES    50
#define MAX_COUPONS   20
#define MAX_PLATFORMS 3
#define TRIE_ALPHA    27   /* a-z + space */
#define KNAPSACK_CAP  200
#define HASHMAP_SIZE  128

/* ============================================================
 * DATA TYPES
 * ============================================================ */

typedef struct {
    char   platform[32];
    double base_price;
    double delivery;
} PlatformPrice;

typedef struct {
    char          name[64];
    PlatformPrice prices[MAX_PLATFORMS];
    int           platform_count;
} Dish;

typedef struct {
    char   code[32];
    char   desc[128];
    char   type[16];    /* "flat" or "percent" */
    double value;
    double max_disc;
    double min_order;
    int    weight;      /* for knapsack capacity simulation */
    char   platform[32]; /* "all" or specific platform */
} Coupon;

/* ============================================================
 * 1. HASHMAP — chaining, O(1) average lookup
 * ============================================================ */

typedef struct HashNode {
    char            key[64];
    int             dish_idx;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode *buckets[HASHMAP_SIZE];
} HashMap;

static unsigned int hash_fn(const char *key) {
    unsigned int h = 5381;
    while (*key) h = ((h << 5) + h) ^ (unsigned char)*key++;
    return h % HASHMAP_SIZE;
}

static void hashmap_insert(HashMap *hm, const char *key, int idx) {
    unsigned int b = hash_fn(key);
    HashNode *node = malloc(sizeof(HashNode));
    strncpy(node->key, key, 63);
    node->key[63] = '\0';
    node->dish_idx = idx;
    node->next = hm->buckets[b];
    hm->buckets[b] = node;
}

/* Returns dish index or -1 */
static int hashmap_get(HashMap *hm, const char *key) {
    unsigned int b = hash_fn(key);
    HashNode *cur = hm->buckets[b];
    while (cur) {
        if (strcasecmp(cur->key, key) == 0) return cur->dish_idx;
        cur = cur->next;
    }
    return -1;
}

/* ============================================================
 * 2. TRIE (Prefix Tree) — O(m) insert/search
 * ============================================================ */

static int char_to_idx(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c == ' ') return 26;
    return -1;
}

typedef struct TrieNode {
    struct TrieNode *children[TRIE_ALPHA];
    int              is_end;
    char             word[64];
} TrieNode;

static TrieNode *trie_new_node(void) {
    TrieNode *n = calloc(1, sizeof(TrieNode));
    return n;
}

static void trie_insert(TrieNode *root, const char *word) {
    TrieNode *cur = root;
    char lower[64];
    int i;
    for (i = 0; word[i] && i < 63; i++)
        lower[i] = tolower((unsigned char)word[i]);
    lower[i] = '\0';

    for (i = 0; lower[i]; i++) {
        int idx = char_to_idx(lower[i]);
        if (idx < 0) continue;
        if (!cur->children[idx])
            cur->children[idx] = trie_new_node();
        cur = cur->children[idx];
    }
    cur->is_end = 1;
    strncpy(cur->word, word, 63);
}

/* DFS collect all words from this node */
static void trie_dfs(TrieNode *node, char results[][64], int *count, int max) {
    if (!node || *count >= max) return;
    if (node->is_end) {
        strncpy(results[(*count)++], node->word, 63);
    }
    for (int i = 0; i < TRIE_ALPHA; i++)
        trie_dfs(node->children[i], results, count, max);
}

/* Returns number of matches; fills results array */
static int trie_search(TrieNode *root, const char *prefix,
                        char results[][64], int max) {
    TrieNode *cur = root;
    char lower[64];
    int i;
    for (i = 0; prefix[i] && i < 63; i++)
        lower[i] = tolower((unsigned char)prefix[i]);
    lower[i] = '\0';

    for (i = 0; lower[i]; i++) {
        int idx = char_to_idx(lower[i]);
        if (idx < 0 || !cur->children[idx]) return 0;
        cur = cur->children[idx];
    }
    int count = 0;
    trie_dfs(cur, results, &count, max);
    return count;
}

/* ============================================================
 * 3. GREEDY — pick single coupon with max discount
 * ============================================================ */

typedef struct {
    Coupon *coupon;
    double  saving;
} GreedyResult;

static GreedyResult greedy_best_coupon(Coupon *coupons, int n,
                                        const char *platform, double total) {
    GreedyResult best = { NULL, 0.0 };
    for (int i = 0; i < n; i++) {
        Coupon *c = &coupons[i];
        if (strcmp(c->platform, "all") != 0 &&
            strcasecmp(c->platform, platform) != 0) continue;
        if (total < c->min_order) continue;

        double saving = 0.0;
        if (strcmp(c->type, "flat") == 0)
            saving = c->value;
        else if (strcmp(c->type, "percent") == 0)
            saving = fmin((total * c->value) / 100.0, c->max_disc);

        if (saving > best.saving) {
            best.saving = saving;
            best.coupon = c;
        }
    }
    return best;
}

/* ============================================================
 * 4. DYNAMIC PROGRAMMING — 0/1 Knapsack
 *    Items = coupons, weight = coupon.weight, value = saving
 *    Capacity = KNAPSACK_CAP
 * ============================================================ */

typedef struct {
    double  max_saving;
    int     used[MAX_COUPONS];  /* indices of selected coupons */
    int     used_count;
} DPResult;

static DPResult dp_knapsack(Coupon *coupons, int n,
                              const char *platform, double total) {
    DPResult result = { 0.0, {0}, 0 };

    /* Filter applicable coupons */
    int valid[MAX_COUPONS], vn = 0;
    for (int i = 0; i < n && vn < MAX_COUPONS; i++) {
        Coupon *c = &coupons[i];
        if (strcmp(c->platform, "all") != 0 &&
            strcasecmp(c->platform, platform) != 0) continue;
        if (total < c->min_order) continue;
        valid[vn++] = i;
    }

    if (vn == 0) return result;

    int W = KNAPSACK_CAP;
    /* dp[i][w] = max discount using first i coupons with capacity w */
    double **dp = malloc((vn + 1) * sizeof(double *));
    for (int i = 0; i <= vn; i++) {
        dp[i] = calloc(W + 1, sizeof(double));
    }

    for (int i = 1; i <= vn; i++) {
        Coupon *c = &coupons[valid[i-1]];
        double saving = 0.0;
        if (strcmp(c->type, "flat") == 0)
            saving = c->value;
        else
            saving = fmin((total * c->value) / 100.0, c->max_disc);

        for (int w = 0; w <= W; w++) {
            dp[i][w] = dp[i-1][w];
            if (c->weight <= w) {
                double candidate = dp[i-1][w - c->weight] + saving;
                if (candidate > dp[i][w])
                    dp[i][w] = candidate;
            }
        }
    }

    result.max_saving = dp[vn][W];

    /* Traceback */
    int w = W;
    for (int i = vn; i > 0; i--) {
        if (dp[i][w] != dp[i-1][w]) {
            result.used[result.used_count++] = valid[i-1];
            w -= coupons[valid[i-1]].weight;
        }
    }

    for (int i = 0; i <= vn; i++) free(dp[i]);
    free(dp);

    return result;
}

/* ============================================================
 * 5. MIN-HEAP (Priority Queue) — rank deals cheapest-first
 * ============================================================ */

typedef struct {
    char   platform[32];
    double final_price;
    double base;
    double delivery;
    double tax;
    double saving;
    char   coupons_used[256];
} Deal;

typedef struct {
    Deal  data[MAX_PLATFORMS * 2];
    int   size;
} MinHeap;

static void heap_swap(MinHeap *h, int a, int b) {
    Deal tmp = h->data[a];
    h->data[a] = h->data[b];
    h->data[b] = tmp;
}

static void heap_bubble_up(MinHeap *h, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->data[p].final_price <= h->data[i].final_price) break;
        heap_swap(h, p, i);
        i = p;
    }
}

static void heap_sink_down(MinHeap *h, int i) {
    while (1) {
        int smallest = i, l = 2*i+1, r = 2*i+2;
        if (l < h->size && h->data[l].final_price < h->data[smallest].final_price)
            smallest = l;
        if (r < h->size && h->data[r].final_price < h->data[smallest].final_price)
            smallest = r;
        if (smallest == i) break;
        heap_swap(h, smallest, i);
        i = smallest;
    }
}

static void heap_insert(MinHeap *h, Deal d) {
    h->data[h->size++] = d;
    heap_bubble_up(h, h->size - 1);
}

static Deal heap_extract_min(MinHeap *h) {
    Deal min = h->data[0];
    h->data[0] = h->data[--h->size];
    heap_sink_down(h, 0);
    return min;
}

/* ============================================================
 * GLOBAL DATA STORE
 * ============================================================ */

static Dish    g_dishes[MAX_DISHES];
static int     g_dish_count = 0;
static Coupon  g_coupons[MAX_COUPONS];
static int     g_coupon_count = 0;
static HashMap g_hashmap;
static TrieNode *g_trie_root = NULL;

static void add_dish(const char *name,
    double sw_price, double sw_del,
    double zo_price, double zo_del,
    double mg_price, double mg_del)
{
    int i = g_dish_count++;
    strncpy(g_dishes[i].name, name, 63);
    g_dishes[i].platform_count = 3;

    strncpy(g_dishes[i].prices[0].platform, "Swiggy", 31);
    g_dishes[i].prices[0].base_price = sw_price;
    g_dishes[i].prices[0].delivery   = sw_del;

    strncpy(g_dishes[i].prices[1].platform, "Zomato", 31);
    g_dishes[i].prices[1].base_price = zo_price;
    g_dishes[i].prices[1].delivery   = zo_del;

    strncpy(g_dishes[i].prices[2].platform, "Magicpin", 31);
    g_dishes[i].prices[2].base_price = mg_price;
    g_dishes[i].prices[2].delivery   = mg_del;

    hashmap_insert(&g_hashmap, name, i);
    trie_insert(g_trie_root, name);
}

static void add_coupon(const char *code, const char *desc, const char *type,
    double value, double max_disc, double min_order, int weight,
    const char *platform)
{
    int i = g_coupon_count++;
    strncpy(g_coupons[i].code,      code,     31);
    strncpy(g_coupons[i].desc,      desc,    127);
    strncpy(g_coupons[i].type,      type,     15);
    strncpy(g_coupons[i].platform,  platform, 31);
    g_coupons[i].value     = value;
    g_coupons[i].max_disc  = max_disc;
    g_coupons[i].min_order = min_order;
    g_coupons[i].weight    = weight;
}

static void init_data(void) {
    memset(&g_hashmap, 0, sizeof(g_hashmap));
    g_trie_root = trie_new_node();

    /* add_dish(name, swiggy_price, swiggy_del, zomato_price, zomato_del, magicpin_price, magicpin_del) */
    add_dish("Butter Chicken",   280, 35, 295, 20, 265, 45);
    add_dish("Pizza",            350, 30, 320, 25, 340, 40);
    add_dish("Biryani",          220, 25, 240, 20, 210, 35);
    add_dish("Burger",           150, 20, 145, 25, 160, 15);
    add_dish("Pasta",            200, 30, 190, 20, 185, 40);
    add_dish("Paneer Tikka",     260, 30, 245, 20, 270, 35);
    add_dish("Dosa",              90, 20,  85, 15,  80, 25);
    add_dish("Noodles",          140, 25, 130, 20, 145, 30);
    add_dish("Chole Bhature",    120, 20, 115, 15, 125, 25);
    add_dish("Masala Dosa",      100, 20,  95, 15,  90, 25);
    add_dish("Palak Paneer",     240, 30, 230, 20, 250, 35);
    add_dish("Dal Makhani",      180, 30, 170, 20, 175, 35);
    add_dish("Pav Bhaji",        130, 20, 125, 15, 120, 25);
    add_dish("Tandoori Chicken", 320, 35, 310, 25, 300, 45);
    add_dish("Idli Sambar",       80, 15,  75, 15,  70, 20);

    /* add_coupon(code, desc, type, value, max_disc, min_order, weight, platform) */
    add_coupon("SAVE50",    "Flat Rs.50 off on orders above Rs.200", "flat",    50,  0,   200, 50,  "all");
    add_coupon("SWIGGY20",  "20% off on Swiggy (max Rs.80)",          "percent", 20,  80,  0,   80,  "Swiggy");
    add_coupon("ZOMATO30",  "30% off on Zomato (max Rs.100)",         "percent", 30,  100, 0,   100, "Zomato");
    add_coupon("MAGIC40",   "Rs.40 off on Magicpin",                  "flat",    40,  0,   0,   40,  "Magicpin");
    add_coupon("FIRST100",  "Rs.100 off - first order only",          "flat",    100, 0,   250, 100, "all");
    add_coupon("WEEKEND15", "15% off weekends (max Rs.60)",           "percent", 15,  60,  0,   60,  "all");
}

/* ============================================================
 * URL DECODE
 * ============================================================ */
static void url_decode(const char *src, char *dst, int max) {
    int i = 0, j = 0;
    while (src[i] && j < max - 1) {
        if (src[i] == '%' && isxdigit(src[i+1]) && isxdigit(src[i+2])) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' '; i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

static void get_query_param(const char *query, const char *key, char *out, int max) {
    out[0] = '\0';
    char search[128];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(query, search);
    if (!p) return;
    p += strlen(search);
    const char *end = strchr(p, '&');
    int len = end ? (int)(end - p) : (int)strlen(p);
    if (len >= max) len = max - 1;
    char raw[512];
    strncpy(raw, p, len); raw[len] = '\0';
    url_decode(raw, out, max);
}

/* ============================================================
 * JSON RESPONSE BUILDERS
 * ============================================================ */

static void json_escape(const char *src, char *dst, int max) {
    int j = 0;
    for (int i = 0; src[i] && j < max - 2; i++) {
        if (src[i] == '"')  { dst[j++] = '\\'; dst[j++] = '"'; }
        else if (src[i] == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static int build_search_response(const char *prefix, char *out, int max) {
    char results[20][64];
    int count = trie_search(g_trie_root, prefix, results, 20);

    int n = snprintf(out, max,
        "{\"algo\":\"Trie\",\"complexity\":\"O(m) where m=%d\","
        "\"prefix\":\"%s\",\"count\":%d,\"results\":[",
        (int)strlen(prefix), prefix, count);

    for (int i = 0; i < count && n < max - 64; i++) {
        char esc[128];
        json_escape(results[i], esc, sizeof(esc));
        n += snprintf(out + n, max - n, "%s\"%s\"", i ? "," : "", esc);
    }
    n += snprintf(out + n, max - n, "]}");
    return n;
}

static int build_prices_response(const char *dish_name, char *out, int max) {
    int idx = hashmap_get(&g_hashmap, dish_name);
    if (idx < 0) {
        return snprintf(out, max,
            "{\"error\":\"Dish not found\",\"algo\":\"HashMap\","
            "\"complexity\":\"O(1)\"}");
    }

    Dish *d = &g_dishes[idx];
    char esc[128];
    json_escape(d->name, esc, sizeof(esc));

    int n = snprintf(out, max,
        "{\"algo\":\"HashMap\",\"complexity\":\"O(1)\","
        "\"hash_bucket\":%u,\"dish\":\"%s\",\"platforms\":[",
        hash_fn(dish_name), esc);

    for (int i = 0; i < d->platform_count && n < max - 200; i++) {
        PlatformPrice *pp = &d->prices[i];
        double tax = pp->base_price * 0.05;
        double total = pp->base_price + pp->delivery + tax;
        n += snprintf(out + n, max - n,
            "%s{\"platform\":\"%s\",\"base\":%.2f,\"delivery\":%.2f,"
            "\"tax\":%.2f,\"total\":%.2f}",
            i ? "," : "",
            pp->platform, pp->base_price, pp->delivery, tax, total);
    }
    n += snprintf(out + n, max - n, "]}");
    return n;
}

static int build_coupons_response(const char *dish_name, const char *platform,
                                   char *out, int max) {
    int idx = hashmap_get(&g_hashmap, dish_name);
    if (idx < 0)
        return snprintf(out, max, "{\"error\":\"Dish not found\"}");

    Dish *d = &g_dishes[idx];
    double base = 0, delivery = 0;
    for (int i = 0; i < d->platform_count; i++) {
        if (strcasecmp(d->prices[i].platform, platform) == 0) {
            base     = d->prices[i].base_price;
            delivery = d->prices[i].delivery;
            break;
        }
    }
    double tax   = base * 0.05;
    double total = base + delivery + tax;

    int n = snprintf(out, max,
        "{\"platform\":\"%s\",\"subtotal\":%.2f,\"coupons\":[", platform, total);

    int first = 1;
    for (int i = 0; i < g_coupon_count; i++) {
        Coupon *c = &g_coupons[i];
        if (strcmp(c->platform, "all") != 0 &&
            strcasecmp(c->platform, platform) != 0) continue;
        if (total < c->min_order) continue;

        double saving = 0;
        if (strcmp(c->type, "flat") == 0) saving = c->value;
        else saving = fmin((total * c->value) / 100.0, c->max_disc);

        char esc_code[64], esc_desc[256];
        json_escape(c->code, esc_code, sizeof(esc_code));
        json_escape(c->desc, esc_desc, sizeof(esc_desc));

        n += snprintf(out + n, max - n,
            "%s{\"code\":\"%s\",\"desc\":\"%s\",\"type\":\"%s\","
            "\"value\":%.2f,\"saving\":%.2f,\"weight\":%d}",
            first ? "" : ",",
            esc_code, esc_desc, c->type, c->value, saving, c->weight);
        first = 0;
    }
    n += snprintf(out + n, max - n, "]}");
    return n;
}

static int build_optimize_response(const char *dish_name, char *out, int max) {
    int idx = hashmap_get(&g_hashmap, dish_name);
    if (idx < 0)
        return snprintf(out, max, "{\"error\":\"Dish not found\"}");

    Dish *d = &g_dishes[idx];
    MinHeap heap; heap.size = 0;

    char greedy_log[1024] = "[";
    char dp_log[1024] = "[";
    int first_g = 1, first_d = 1;

    for (int i = 0; i < d->platform_count; i++) {
        PlatformPrice *pp = &d->prices[i];
        double tax   = pp->base_price * 0.05;
        double total = pp->base_price + pp->delivery + tax;

        /* --- Greedy --- */
        GreedyResult gr = greedy_best_coupon(g_coupons, g_coupon_count,
                                              pp->platform, total);
        double greedy_save = gr.saving;

        /* --- DP Knapsack --- */
        DPResult dp = dp_knapsack(g_coupons, g_coupon_count,
                                   pp->platform, total);
        double best_save = fmax(greedy_save, dp.max_saving);
        double final_price = fmax(0.0, total - best_save);

        /* Build coupons_used string */
        char coupons_used[256] = "";
        if (dp.max_saving >= greedy_save && dp.used_count > 0) {
            for (int k = 0; k < dp.used_count; k++) {
                if (k) strncat(coupons_used, "+", sizeof(coupons_used)-strlen(coupons_used)-1);
                strncat(coupons_used, g_coupons[dp.used[k]].code,
                        sizeof(coupons_used)-strlen(coupons_used)-1);
            }
        } else if (gr.coupon) {
            strncpy(coupons_used, gr.coupon->code, sizeof(coupons_used)-1);
        }

        /* Greedy log entry */
        char esc_p[64];
        json_escape(pp->platform, esc_p, sizeof(esc_p));
        int glen = strlen(greedy_log);
        snprintf(greedy_log + glen, sizeof(greedy_log) - glen,
            "%s{\"platform\":\"%s\",\"greedy_saving\":%.2f,\"coupon\":\"%s\"}",
            first_g ? "" : ",",
            esc_p, greedy_save, gr.coupon ? gr.coupon->code : "none");
        first_g = 0;

        /* DP log entry */
        int dlen = strlen(dp_log);
        char dp_coupons[256] = "";
        for (int k = 0; k < dp.used_count; k++) {
            if (k) strncat(dp_coupons, ",", sizeof(dp_coupons)-strlen(dp_coupons)-1);
            strncat(dp_coupons, g_coupons[dp.used[k]].code,
                    sizeof(dp_coupons)-strlen(dp_coupons)-1);
        }
        snprintf(dp_log + dlen, sizeof(dp_log) - dlen,
            "%s{\"platform\":\"%s\",\"dp_saving\":%.2f,\"dp_coupons\":\"%s\"}",
            first_d ? "" : ",",
            esc_p, dp.max_saving, dp_coupons);
        first_d = 0;

        /* Insert into MinHeap */
        Deal deal;
        strncpy(deal.platform, pp->platform, 31);
        deal.final_price = final_price;
        deal.base        = pp->base_price;
        deal.delivery    = pp->delivery;
        deal.tax         = tax;
        deal.saving      = best_save;
        strncpy(deal.coupons_used, coupons_used, 255);
        heap_insert(&heap, deal);
    }

    strncat(greedy_log, "]", sizeof(greedy_log)-strlen(greedy_log)-1);
    strncat(dp_log, "]", sizeof(dp_log)-strlen(dp_log)-1);

    /* Extract sorted from heap */
    int n = snprintf(out, max,
        "{\"dish\":\"%s\","
        "\"algo_trace\":{"
          "\"hashmap\":{\"complexity\":\"O(1)\",\"bucket\":%u},"
          "\"trie\":{\"complexity\":\"O(m)\"},"
          "\"greedy\":{\"complexity\":\"O(n)\",\"log\":%s},"
          "\"dp_knapsack\":{\"complexity\":\"O(n*W) n=%d W=%d\",\"log\":%s},"
          "\"min_heap\":{\"complexity\":\"O(n log n)\",\"size\":%d}"
        "},"
        "\"rankings\":[",
        d->name, hash_fn(dish_name),
        greedy_log,
        g_coupon_count, KNAPSACK_CAP, dp_log,
        heap.size);

    int rank = 1;
    while (heap.size > 0 && n < max - 512) {
        Deal deal = heap_extract_min(&heap);
        char esc_p[64], esc_c[256];
        json_escape(deal.platform, esc_p, sizeof(esc_p));
        json_escape(deal.coupons_used, esc_c, sizeof(esc_c));
        n += snprintf(out + n, max - n,
            "%s{\"rank\":%d,\"platform\":\"%s\",\"base\":%.2f,"
            "\"delivery\":%.2f,\"tax\":%.2f,\"saving\":%.2f,"
            "\"final_price\":%.2f,\"coupons_used\":\"%s\"}",
            rank > 1 ? "," : "",
            rank, esc_p, deal.base, deal.delivery,
            deal.tax, deal.saving, deal.final_price, esc_c);
        rank++;
    }
    n += snprintf(out + n, max - n, "]}");
    return n;
}

/* ============================================================
 * HTTP SERVER
 * ============================================================ */

static void send_response(int client_fd, int status,
                           const char *body, int body_len) {
    char header[512];
    const char *status_text = (status == 200) ? "OK" :
                              (status == 404) ? "Not Found" : "Bad Request";
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, body_len);
    write(client_fd, header, hlen);
    write(client_fd, body, body_len);
}

static void handle_request(int client_fd) {
    char req[BUF_SIZE];
    int n = read(client_fd, req, BUF_SIZE - 1);
    if (n <= 0) { close(client_fd); return; }
    req[n] = '\0';

    /* Parse method and path */
    char method[8], path[512], query[512] = "";
    sscanf(req, "%7s %511s", method, path);

    /* Handle OPTIONS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client_fd, 200, "{}", 2);
        close(client_fd); return;
    }

    /* Split path from query string */
    char *q = strchr(path, '?');
    if (q) { strncpy(query, q + 1, sizeof(query) - 1); *q = '\0'; }

    char body[BUF_SIZE * 2];
    int body_len = 0;

    if (strcmp(path, "/api/search") == 0) {
        char prefix[128] = "";
        get_query_param(query, "q", prefix, sizeof(prefix));
        body_len = build_search_response(prefix, body, sizeof(body));

    } else if (strcmp(path, "/api/prices") == 0) {
        char dish[128] = "";
        get_query_param(query, "dish", dish, sizeof(dish));
        body_len = build_prices_response(dish, body, sizeof(body));

    } else if (strcmp(path, "/api/coupons") == 0) {
        char dish[128] = "", platform[64] = "";
        get_query_param(query, "dish", dish, sizeof(dish));
        get_query_param(query, "platform", platform, sizeof(platform));
        body_len = build_coupons_response(dish, platform, body, sizeof(body));

    } else if (strcmp(path, "/api/optimize") == 0) {
        /* POST body or GET param */
        char dish[128] = "";
        /* Try GET first */
        get_query_param(query, "dish", dish, sizeof(dish));
        /* Try POST body */
        if (dish[0] == '\0') {
            char *body_start = strstr(req, "\r\n\r\n");
            if (body_start) {
                body_start += 4;
                char raw[256];
                snprintf(raw, sizeof(raw), "dish=%s", body_start);
                get_query_param(raw + 5, "dish", dish, sizeof(dish));
                /* Try JSON key */
                char *dp = strstr(body_start, "\"dish\"");
                if (dp) {
                    dp = strchr(dp, ':');
                    if (dp) {
                        dp++;
                        while (*dp == ' ' || *dp == '"') dp++;
                        int k = 0;
                        while (*dp && *dp != '"' && k < 127) dish[k++] = *dp++;
                        dish[k] = '\0';
                    }
                }
            }
        }
        body_len = build_optimize_response(dish, body, sizeof(body));

    } else if (strcmp(path, "/api/dishes") == 0) {
        /* List all dishes */
        body_len = snprintf(body, sizeof(body), "{\"dishes\":[");
        for (int i = 0; i < g_dish_count; i++) {
            char esc[128];
            json_escape(g_dishes[i].name, esc, sizeof(esc));
            body_len += snprintf(body + body_len, sizeof(body) - body_len,
                "%s\"%s\"", i ? "," : "", esc);
        }
        body_len += snprintf(body + body_len, sizeof(body) - body_len,
            "],\"count\":%d}", g_dish_count);

    } else {
        body_len = snprintf(body, sizeof(body),
            "{\"error\":\"Unknown endpoint\","
            "\"available\":[\"/api/search\",\"/api/prices\","
            "\"/api/coupons\",\"/api/optimize\",\"/api/dishes\"]}");
        send_response(client_fd, 404, body, body_len);
        close(client_fd); return;
    }

    send_response(client_fd, 200, body, body_len);
    close(client_fd);
}

int main(void) {
    init_data();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORT)
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(server_fd, 16);

    printf("MealMax C Backend running on http://localhost:%d\n", PORT);
    printf("Loaded %d dishes, %d coupons\n", g_dish_count, g_coupon_count);
    printf("DSA: HashMap + Trie + Greedy + DP-Knapsack + MinHeap\n\n");
    printf("Endpoints:\n");
    printf("  GET /api/dishes\n");
    printf("  GET /api/search?q=<prefix>\n");
    printf("  GET /api/prices?dish=<name>\n");
    printf("  GET /api/coupons?dish=<name>&platform=<platform>\n");
    printf("  GET /api/optimize?dish=<name>\n\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &clen);
        if (client_fd < 0) continue;
        printf("[REQ] %s\n", inet_ntoa(client_addr.sin_addr));
        fflush(stdout);
        handle_request(client_fd);
    }

    return 0;
}
