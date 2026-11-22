import { useMemo } from 'react';
import { Level, fmt, fromScale } from '../api/client';

interface Props {
  bids: Level[];
  asks: Level[];
  onPrice?: (price: number) => void;
}

export default function OrderBook({ bids, asks, onPrice }: Props) {
  const { bidRows, askRows, maxTotal, spread, spreadPct, midPrice, bestBid, bestAsk } = useMemo(() => {
    const numLevels = 12;
    
    // Sort: bids descending (highest first), asks ascending (lowest first)
    const sortedBids = [...bids].sort((a, b) => b.price - a.price).slice(0, numLevels);
    const sortedAsks = [...asks].sort((a, b) => a.price - b.price).slice(0, numLevels);

    // Calculate cumulative totals
    let bidTotal = 0;
    let askTotal = 0;
    const bidRows = sortedBids.map(b => ({ ...b, total: bidTotal += b.quantity }));
    const askRows = sortedAsks.map(a => ({ ...a, total: askTotal += a.quantity })).reverse(); // Reverse for display (lowest ask at bottom)

    const maxTotal = Math.max(bidTotal, askTotal) || 1;

    // Best bid/ask and spread
    const bestBid = sortedBids[0]?.price ?? null;
    const bestAsk = sortedAsks[0]?.price ?? null;
    
    let spread = 0;
    let spreadPct = 0;
    let midPrice = 0;
    
    if (bestBid !== null && bestAsk !== null) {
      const bidPrice = fromScale(bestBid);
      const askPrice = fromScale(bestAsk);
      spread = askPrice - bidPrice;
      midPrice = (bidPrice + askPrice) / 2;
      spreadPct = midPrice > 0 ? (spread / midPrice) * 100 : 0;
    }

    return { bidRows, askRows, maxTotal, spread, spreadPct, midPrice, bestBid, bestAsk };
  }, [bids, asks]);

  const hasBids = bidRows.length > 0;
  const hasAsks = askRows.length > 0;
  const hasBothSides = hasBids && hasAsks;

  return (
    <div className="card h-full flex flex-col">
      <div className="card-header">
        <span>Order Book</span>
        <span className="mono text-[10px] text-text-3">
          {hasBothSides ? `Spread: ${spread.toFixed(2)} (${spreadPct.toFixed(3)}%)` : 'Spread: N/A'}
        </span>
      </div>

      {/* Column headers */}
      <div className="grid grid-cols-3 px-3 py-1.5 text-[10px] text-text-3 border-b border-border">
        <span>Price</span>
        <span className="text-right">Size</span>
        <span className="text-right">Total</span>
      </div>

      <div className="flex-1 flex flex-col overflow-hidden text-xs">
        {/* Asks (sells) - displayed with lowest at bottom near spread */}
        <div className="flex-1 flex flex-col justify-end overflow-hidden">
          {!hasAsks ? (
            <div className="p-3 text-center text-text-3 text-xs">No asks</div>
          ) : (
            askRows.map((level) => (
              <PriceRow
                key={level.price}
                level={level}
                maxTotal={maxTotal}
                side="ask"
                isBest={level.price === bestAsk}
                onClick={() => onPrice?.(level.price)}
              />
            ))
          )}
        </div>

        {/* Spread bar */}
        <div className="py-2 px-3 bg-bg-3 border-y border-border">
          <div className="flex items-center justify-between text-sm">
            {hasAsks ? (
              <div>
                <span className="font-semibold mono tabular text-short">{fmt.price(bestAsk!)}</span>
                <span className="text-[10px] text-text-3 ml-1">Best Ask</span>
              </div>
            ) : (
              <span className="text-text-3 text-xs">—</span>
            )}
            
            {hasBothSides && (
              <div className="text-center text-[10px]">
                <div className="text-text-3">Mid</div>
                <div className="mono tabular text-text-2">{midPrice.toFixed(2)}</div>
              </div>
            )}
            
            {hasBids ? (
              <div className="text-right">
                <span className="font-semibold mono tabular text-long">{fmt.price(bestBid!)}</span>
                <span className="text-[10px] text-text-3 ml-1">Best Bid</span>
              </div>
            ) : (
              <span className="text-text-3 text-xs">—</span>
            )}
          </div>
        </div>

        {/* Bids (buys) */}
        <div className="flex-1 overflow-hidden">
          {!hasBids ? (
            <div className="p-3 text-center text-text-3 text-xs">No bids</div>
          ) : (
            bidRows.map((level) => (
              <PriceRow
                key={level.price}
                level={level}
                maxTotal={maxTotal}
                side="bid"
                isBest={level.price === bestBid}
                onClick={() => onPrice?.(level.price)}
              />
            ))
          )}
        </div>
      </div>
    </div>
  );
}

interface PriceRowProps {
  level: Level & { total: number };
  maxTotal: number;
  side: 'bid' | 'ask';
  isBest: boolean;
  onClick: () => void;
}

function PriceRow({ level, maxTotal, side, isBest, onClick }: PriceRowProps) {
  const pct = (level.total / maxTotal) * 100;
  const isBid = side === 'bid';

  return (
    <div
      onClick={onClick}
      className={`relative grid grid-cols-3 px-3 py-[3px] cursor-pointer hover:bg-bg-3 transition-colors ${
        isBest ? 'bg-bg-3/50' : ''
      }`}
    >
      {/* Depth bar */}
      <div
        className={`absolute inset-y-0 pointer-events-none ${isBid ? 'right-0' : 'left-0'}`}
        style={{
          width: `${pct}%`,
          background: isBid ? 'rgba(2, 192, 118, 0.15)' : 'rgba(246, 70, 93, 0.15)',
        }}
      />
      
      <span className={`relative mono tabular font-medium ${isBid ? 'text-long' : 'text-short'}`}>
        {fmt.price(level.price)}
        {isBest && <span className="ml-1 text-[9px]">★</span>}
      </span>
      <span className="relative text-right mono tabular text-text-1">
        {fmt.qty(level.quantity)}
      </span>
      <span className="relative text-right mono tabular text-text-3">
        {fmt.qty(level.total)}
      </span>
    </div>
  );
}
