import { useState, useCallback, useEffect } from 'react';
import { Order, Trade, api } from './api/client';
import { useBook, useTrades, useHealth, useStats, useLatency, useBalance } from './hooks/useData';
import Header from './components/Header';
import OrderBook from './components/OrderBook';
import Chart from './components/Chart';
import Depth from './components/Depth';
import Trades from './components/Trades';
import OrderForm from './components/OrderForm';
import OpenOrders from './components/OpenOrders';
import Account from './components/Account';

const SYMBOLS = ['BTC-USD'];
const ACCOUNTS = ['Alice', 'Bob'];

export default function App() {
  const [symbol, setSymbol] = useState(SYMBOLS[0]);
  const [account, setAccount] = useState(ACCOUNTS[0]);
  const [orders, setOrders] = useState<Order[]>([]);
  const [resetLoading, setResetLoading] = useState(false);
  const [resetKey, setResetKey] = useState(0);

  // Data hooks
  const { data: book, refresh: refreshBook } = useBook(symbol);
  const { data: trades, refresh: refreshTrades } = useTrades(symbol);
  const { data: balance, refresh: refreshBalance } = useBalance(account);
  const health = useHealth();
  const stats = useStats();
  const latency = useLatency();

  // Handle new order
  const onOrder = useCallback((order: Order, orderTrades: Trade[]) => {
    setOrders(prev => {
      const exists = prev.find(o => o.id === order.id);
      if (exists) {
        return prev.map(o => o.id === order.id ? order : o);
      }
      return [...prev, order];
    });
    // Refresh data after order
    setTimeout(() => {
      refreshBook();
      refreshTrades();
      refreshBalance();
    }, 100);
  }, [refreshBook, refreshTrades, refreshBalance]);

  // Handle order cancellation
  const onCancel = useCallback((order: Order) => {
    setOrders(prev => prev.map(o => o.id === order.id ? order : o));
    setTimeout(() => {
      refreshBook();
      refreshBalance();
    }, 100);
  }, [refreshBook, refreshBalance]);

  // Handle reset
  const handleReset = useCallback(async () => {
    if (!confirm('Reset all exchange data? This clears orders, trades, and event log.')) {
      return;
    }

    setResetLoading(true);
    try {
      const result = await api.reset();
      if (result.ok) {
        // Clear local state
        setOrders([]);
        setResetKey(k => k + 1);
        
        // Refresh all data
        setTimeout(() => {
          refreshBook();
          refreshTrades();
          refreshBalance();
        }, 200);
        
        alert('Reset complete!');
      } else {
        alert('Reset failed');
      }
    } catch (e: any) {
      console.error('Reset error:', e);
      alert('Reset failed: ' + (e.message || 'Unknown error'));
    }
    setResetLoading(false);
  }, [refreshBook, refreshTrades, refreshBalance]);

  // Keyboard shortcut: Ctrl+Shift+R
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.ctrlKey && e.shiftKey && e.key === 'R') {
        e.preventDefault();
        handleReset();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [handleReset]);

  // Refresh balance when account changes
  useEffect(() => {
    refreshBalance();
  }, [account, refreshBalance]);

  const myOrders = orders.filter(o => o.account_id === account);
  const hasDepthData = (book?.bids.length ?? 0) > 0 || (book?.asks.length ?? 0) > 0;

  return (
    <div className="h-screen flex flex-col bg-bg-1" key={resetKey}>
      <Header
        symbol={symbol}
        symbols={SYMBOLS}
        onSymbol={setSymbol}
        health={health}
        stats={stats}
        trades={trades}
        latency={latency}
        onReset={handleReset}
        resetLoading={resetLoading}
      />

      <main className="flex-1 flex overflow-hidden">
        {/* Left: Order Book */}
        <div className="w-72 border-r border-border flex flex-col">
          <OrderBook 
            bids={book?.bids ?? []} 
            asks={book?.asks ?? []} 
          />
        </div>

        {/* Center: Charts */}
        <div className="flex-1 flex flex-col p-2 gap-2 min-w-0">
          {/* Price Chart */}
          <div className="flex-1 min-h-0">
            <Chart trades={trades} />
          </div>
          
          {/* Depth Chart (only shows when data exists) */}
          {hasDepthData && (
            <div className="h-32">
              <Depth bids={book?.bids ?? []} asks={book?.asks ?? []} />
            </div>
          )}
        </div>

        {/* Right: Trading Panel */}
        <div className="w-80 border-l border-border flex flex-col">
          {/* Account with Balances */}
          <div className="border-b border-border">
            <Account 
              account={account} 
              accounts={ACCOUNTS} 
              balance={balance}
              onChange={setAccount}
              onRefresh={refreshBalance}
            />
          </div>
          
          {/* Order Form */}
          <div className="border-b border-border">
            <OrderForm 
              symbol={symbol} 
              account={account} 
              balance={balance}
              onOrder={onOrder} 
            />
          </div>

          {/* Open Orders with lifecycle */}
          <div className="flex-1 min-h-0">
            <OpenOrders orders={myOrders} onCancel={onCancel} />
          </div>
        </div>

        {/* Far Right: Trades */}
        <div className="w-56 border-l border-border">
          <Trades trades={trades} account={account} />
        </div>
      </main>

      {/* Footer with engine status */}
      <footer className="h-6 bg-bg-2 border-t border-border flex items-center justify-between px-4 text-[10px] text-text-3">
        <div className="flex items-center gap-4">
          <span>Aztec Exchange v1.0</span>
          <span>•</span>
          <span>Account: <span className="text-text-2">{account}</span></span>
        </div>
        <div className="flex items-center gap-4">
          {stats?.sequence && (
            <>
              <span>Seq: <span className="mono text-text-2">{stats.sequence}</span></span>
              <span>•</span>
            </>
          )}
          <span className={health?.engine_connected ? 'text-long' : 'text-short'}>
            ● {health?.engine_connected ? 'Engine Connected' : 'Engine Offline'}
          </span>
        </div>
      </footer>
    </div>
  );
}
