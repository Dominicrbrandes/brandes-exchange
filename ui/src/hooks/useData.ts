import { useState, useEffect, useCallback } from 'react';
import { api, Book, Trade, Health, Stats, Balance, getLatencyStats } from '../api/client';

export function useBook(symbol: string, intervalMs = 500) {
  const [data, setData] = useState<Book | null>(null);
  
  const refresh = useCallback(async () => {
    try {
      setData(await api.book(symbol));
    } catch (e) {
      console.error('Book fetch failed:', e);
    }
  }, [symbol]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, intervalMs);
    return () => clearInterval(interval);
  }, [refresh, intervalMs]);

  return { data, refresh };
}

export function useTrades(symbol: string, intervalMs = 1000) {
  const [data, setData] = useState<Trade[]>([]);

  const refresh = useCallback(async () => {
    try {
      const result = await api.trades(symbol, 100);
      setData(result.trades);
    } catch (e) {
      console.error('Trades fetch failed:', e);
    }
  }, [symbol]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, intervalMs);
    return () => clearInterval(interval);
  }, [refresh, intervalMs]);

  return { data, refresh };
}

export function useHealth(intervalMs = 3000) {
  const [data, setData] = useState<Health | null>(null);

  useEffect(() => {
    const fetch = async () => {
      try {
        setData(await api.health());
      } catch {
        setData({ status: 'error', engine_connected: false });
      }
    };
    fetch();
    const interval = setInterval(fetch, intervalMs);
    return () => clearInterval(interval);
  }, [intervalMs]);

  return data;
}

export function useStats(intervalMs = 1000) {
  const [data, setData] = useState<Stats | null>(null);

  useEffect(() => {
    const fetch = async () => {
      try {
        setData(await api.stats());
      } catch (e) {
        console.error('Stats fetch failed:', e);
      }
    };
    fetch();
    const interval = setInterval(fetch, intervalMs);
    return () => clearInterval(interval);
  }, [intervalMs]);

  return data;
}

export function useBalance(accountId: string, intervalMs = 500) {
  const [data, setData] = useState<Balance | null>(null);

  const refresh = useCallback(async () => {
    try {
      setData(await api.balances(accountId));
    } catch (e) {
      // Fallback to default if endpoint not available
      setData({ usd: 10000000000000, btc: 1000000000, usd_reserved: 0, btc_reserved: 0 });
    }
  }, [accountId]);

  useEffect(() => {
    refresh();
    const interval = setInterval(refresh, intervalMs);
    return () => clearInterval(interval);
  }, [refresh, intervalMs]);

  return { data, refresh };
}

export function useLatency(intervalMs = 2000) {
  const [data, setData] = useState({ p50: 0, p95: 0, rps: 0 });

  useEffect(() => {
    const interval = setInterval(() => {
      setData(getLatencyStats());
    }, intervalMs);
    return () => clearInterval(interval);
  }, [intervalMs]);

  return data;
}
