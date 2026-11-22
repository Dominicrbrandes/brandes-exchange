import { Trade, fmt } from '../api/client';

interface Props {
  trades: Trade[];
  account: string;
}

export default function Trades({ trades, account }: Props) {
  return (
    <div className="card h-full flex flex-col">
      <div className="card-header">
        <span>Recent Trades</span>
        <span className="text-[10px] font-normal text-text-3">{trades.length}</span>
      </div>

      {/* Column headers */}
      <div className="grid grid-cols-3 px-3 py-1.5 text-[10px] text-text-3 border-b border-border">
        <span>Price</span>
        <span className="text-right">Size</span>
        <span className="text-right">Time</span>
      </div>

      <div className="flex-1 overflow-y-auto">
        {trades.length === 0 ? (
          <div className="p-6 text-center text-text-3 text-sm">No trades yet</div>
        ) : (
          trades.slice(0, 50).map((trade, index, arr) => {
            // Determine direction by comparing to next trade (older)
            const nextTrade = arr[index + 1];
            const isBuyTrade = nextTrade ? trade.price >= nextTrade.price : true;
            const isMyTrade = trade.buyer_account_id === account || trade.seller_account_id === account;
            
            return (
              <div
                key={trade.id}
                className={`grid grid-cols-3 px-3 py-1 text-xs hover:bg-bg-3 transition-colors ${
                  isMyTrade ? 'bg-bg-3/50' : ''
                }`}
              >
                <span className={`mono tabular ${isBuyTrade ? 'text-long' : 'text-short'}`}>
                  {fmt.price(trade.price)}
                </span>
                <span className="text-right mono tabular text-text-1">
                  {fmt.qty(trade.quantity)}
                </span>
                <span className="text-right text-text-3 mono text-[10px]">
                  {fmt.time(trade.timestamp_ns)}
                </span>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}
