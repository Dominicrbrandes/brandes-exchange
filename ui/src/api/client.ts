const API = '/api/v1';
const KEY = 'test-key-1';
const ADMIN_KEY = 'admin-key-1';

export const SCALE = 1e8;
export const toScale = (n: number) => Math.round(n * SCALE);
export const fromScale = (n: number) => n / SCALE;

export type TimeInForce = 'GTC' | 'IOC' | 'FOK';

export interface Level { price: number; quantity: number; }
export interface Book { symbol: string; bids: Level[]; asks: Level[]; }
export interface Order {
  id: number;
  account_id: string;
  symbol: string;
  side: 'BUY' | 'SELL';
  type: 'LIMIT';
  price: number;
  quantity: number;
  remaining_qty: number;
  status: 'NEW' | 'PARTIAL' | 'FILLED' | 'CANCELLED' | 'REJECTED';
  time_in_force?: TimeInForce;
  timestamp_ns: number;
  reject_reason?: string;
}
export interface Trade {
  id: number;
  symbol: string;
  price: number;
  quantity: number;
  timestamp_ns: number;
  buyer_account_id: string;
  seller_account_id: string;
}
export interface Stats {
  total_orders: number;
  total_trades: number;
  sequence?: number;
}
export interface Health {
  status: string;
  engine_connected: boolean;
}

export interface Balance {
  usd: number;
  btc: number;
  usd_reserved: number;
  btc_reserved: number;
}

// Formatting
export const fmt = {
  price: (n: number, decimals = 2) => fromScale(n).toLocaleString('en-US', { minimumFractionDigits: decimals, maximumFractionDigits: decimals }),
  qty: (n: number, decimals = 4) => fromScale(n).toLocaleString('en-US', { minimumFractionDigits: decimals, maximumFractionDigits: decimals }),
  usd: (n: number) => '$' + n.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 }),
  time: (ns: number) => new Date(ns / 1e6).toLocaleTimeString('en-US', { hour12: false }),
};

// Latency tracking
let latencies: number[] = [];
export const getLatencyStats = () => {
  if (latencies.length === 0) return { p50: 0, p95: 0, rps: 0 };
  const sorted = [...latencies].sort((a, b) => a - b);
  const result = {
    p50: sorted[Math.floor(sorted.length * 0.5)] || 0,
    p95: sorted[Math.floor(sorted.length * 0.95)] || 0,
    rps: latencies.length,
  };
  latencies = [];
  return result;
};

async function call<T>(path: string, opts?: RequestInit): Promise<T> {
  const start = performance.now();
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  
  // Add API key for protected endpoints
  if (path.includes('/admin/')) {
    headers['X-API-Key'] = ADMIN_KEY;
  } else if (!path.match(/^\/(health|book|trades)/)) {
    headers['X-API-Key'] = KEY;
  }

  const res = await fetch(API + path, { ...opts, headers });
  latencies.push(performance.now() - start);

  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.detail || `HTTP ${res.status}`);
  }
  return res.json();
}

export const api = {
  health: () => call<Health>('/health'),
  book: (symbol: string) => call<Book>(`/book/${symbol}`),
  trades: (symbol: string, limit = 100) => call<{ trades: Trade[] }>(`/trades/${symbol}?limit=${limit}`),
  stats: () => call<Stats>('/stats'),
  
  place: (order: {
    account_id: string;
    symbol: string;
    side: 'BUY' | 'SELL';
    price: number;
    quantity: number;
    time_in_force?: TimeInForce;
  }) => call<{ order: Order; trades: Trade[] }>('/orders', {
    method: 'POST',
    body: JSON.stringify({
      ...order,
      type: 'LIMIT',
      idempotency_key: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
    }),
  }),

  cancel: (orderId: number) => call<Order>(`/orders/${orderId}`, { method: 'DELETE' }),

  // Cancel/replace: new price and new total quantity, same order id
  replace: (orderId: number, price: number, quantity: number) =>
    call<{ order: Order; trades: Trade[] }>(`/orders/${orderId}`, {
      method: 'PATCH',
      body: JSON.stringify({ price, quantity }),
    }),

  // Balance from engine (source of truth)
  balances: (accountId: string) => call<Balance>(`/accounts/${accountId}/balances`),

  // Admin reset
  reset: () => call<{ ok: boolean }>('/admin/reset', { method: 'POST' }),
};

// Calculate balance from orders and trades (fallback / local calculation)
export function calculateBalance(
  orders: Order[],
  trades: Trade[],
  account: string
): { usd: number; btc: number; usdReserved: number; btcReserved: number } {
  // Starting balance
  let usd = 100000;
  let btc = 10;
  let usdReserved = 0;
  let btcReserved = 0;

  // Process trades
  for (const trade of trades) {
    const qty = fromScale(trade.quantity);
    const price = fromScale(trade.price);
    const value = qty * price;

    if (trade.buyer_account_id === account) {
      usd -= value;
      btc += qty;
    }
    if (trade.seller_account_id === account) {
      usd += value;
      btc -= qty;
    }
  }

  // Process open orders (reserved amounts)
  for (const order of orders) {
    if (order.account_id !== account) continue;
    if (order.status !== 'NEW' && order.status !== 'PARTIAL') continue;

    const remainingQty = fromScale(order.remaining_qty);
    const price = fromScale(order.price);

    if (order.side === 'BUY') {
      usdReserved += remainingQty * price;
    } else {
      btcReserved += remainingQty;
    }
  }

  return { usd, btc, usdReserved, btcReserved };
}
