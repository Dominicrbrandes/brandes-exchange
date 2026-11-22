import { Zap, Wifi, WifiOff, Activity, RotateCcw } from 'lucide-react';
import { Health, Stats, Trade, fromScale, fmt } from '../api/client';

interface Props {
  symbol: string;
  symbols: string[];
  onSymbol: (s: string) => void;
  health: Health | null;
  stats: Stats | null;
  trades: Trade[];
  latency: { p50: number; p95: number; rps: number };
  onReset?: () => void;
  resetLoading?: boolean;
}

export default function Header({ symbol, symbols, onSymbol, health, stats, trades, latency, onReset, resetLoading }: Props) {
  const connected = health?.engine_connected ?? false;

  // Calculate stats from trades
  const prices = trades.map(t => fromScale(t.price));
  const lastPrice = prices[0] ?? 0;
  const firstPrice = prices[prices.length - 1] ?? lastPrice;
  const high24h = prices.length > 0 ? Math.max(...prices) : 0;
  const low24h = prices.length > 0 ? Math.min(...prices) : 0;
  const change = firstPrice > 0 ? ((lastPrice - firstPrice) / firstPrice) * 100 : 0;
  const volume = trades.reduce((sum, t) => sum + fromScale(t.quantity), 0);

  return (
    <header className="h-14 bg-bg-2 border-b border-border flex items-center px-4 gap-4">
      {/* Logo */}
      <div className="flex items-center gap-2 pr-4 border-r border-border">
        <div className="w-8 h-8 rounded bg-long flex items-center justify-center">
          <Zap size={18} className="text-bg-1" />
        </div>
        <span className="font-semibold text-text-1">Aztec</span>
      </div>

      {/* Symbol selector */}
      <div className="flex border-r border-border pr-4">
        {symbols.map(s => (
          <button
            key={s}
            onClick={() => onSymbol(s)}
            className={`px-3 py-1.5 text-sm font-medium rounded transition-colors ${
              s === symbol ? 'bg-bg-3 text-text-1' : 'text-text-2 hover:text-text-1'
            }`}
          >
            {s.replace('-USD', '')}
          </button>
        ))}
      </div>

      {/* Price display */}
      <div className="flex items-center gap-3 pr-4 border-r border-border">
        <span className="text-xl font-semibold mono tabular text-text-1">
          {lastPrice > 0 ? fmt.usd(lastPrice) : '—'}
        </span>
        {lastPrice > 0 && (
          <span className={`text-sm font-medium ${change >= 0 ? 'text-long' : 'text-short'}`}>
            {change >= 0 ? '+' : ''}{change.toFixed(2)}%
          </span>
        )}
      </div>

      {/* 24h stats */}
      <div className="flex gap-5 text-xs pr-4 border-r border-border">
        <Stat label="24h High" value={high24h > 0 ? fmt.usd(high24h) : '—'} />
        <Stat label="24h Low" value={low24h > 0 ? fmt.usd(low24h) : '—'} />
        <Stat label="24h Vol" value={volume > 0 ? volume.toFixed(4) : '—'} />
        <Stat label="Trades" value={trades.length.toString()} highlight />
      </div>

      {/* Latency stats */}
      <div className="flex gap-4 text-xs pr-4 border-r border-border">
        <div className="flex items-center gap-1.5">
          <Activity size={12} className="text-text-3" />
          <span className="text-text-2">p50</span>
          <span className="mono tabular text-text-1">{latency.p50.toFixed(0)}ms</span>
        </div>
        <div>
          <span className="text-text-2">p95</span>
          <span className="mono tabular text-text-1 ml-1">{latency.p95.toFixed(0)}ms</span>
        </div>
      </div>

      {/* Engine stats */}
      {stats && (
        <div className="flex gap-4 text-xs pr-4 border-r border-border">
          <Stat label="Orders" value={stats.total_orders.toString()} />
          <Stat label="Seq" value={stats.sequence?.toString() ?? '—'} />
        </div>
      )}

      {/* Reset button */}
      {onReset && (
        <button
          onClick={onReset}
          disabled={resetLoading}
          className="flex items-center gap-1.5 px-2.5 py-1.5 text-xs text-text-3 hover:text-short hover:bg-short/10 rounded transition-colors disabled:opacity-50"
          title="Reset exchange (Ctrl+Shift+R)"
        >
          <RotateCcw size={12} className={resetLoading ? 'animate-spin' : ''} />
          {resetLoading ? 'Resetting...' : 'Reset'}
        </button>
      )}

      {/* Connection status */}
      <div className={`flex items-center gap-1.5 ml-auto px-2.5 py-1 rounded text-xs font-medium ${
        connected ? 'bg-long/10 text-long' : 'bg-short/10 text-short'
      }`}>
        {connected ? <Wifi size={12} /> : <WifiOff size={12} />}
        {connected ? 'Connected' : 'Offline'}
      </div>
    </header>
  );
}

function Stat({ label, value, highlight }: { label: string; value: string; highlight?: boolean }) {
  return (
    <div>
      <div className="text-text-3">{label}</div>
      <div className={`mono tabular ${highlight ? 'text-long' : 'text-text-1'}`}>{value}</div>
    </div>
  );
}
