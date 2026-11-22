import { useState, useEffect } from 'react';
import type { TimeInForce } from '../api/client';
import { api, toScale, Order, Trade, Balance, fromScale } from '../api/client';

interface Props {
  symbol: string;
  account: string;
  balance: Balance | null;
  onOrder?: (order: Order, trades: Trade[]) => void;
}

export default function OrderForm({ symbol, account, balance, onOrder }: Props) {
  const [side, setSide] = useState<'BUY' | 'SELL'>('BUY');
  const [tif, setTif] = useState<TimeInForce>('GTC');
  const [price, setPrice] = useState('');
  const [quantity, setQuantity] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  // Clear messages when inputs change
  useEffect(() => {
    setError('');
    setSuccess('');
  }, [price, quantity, side]);

  const priceNum = parseFloat(price) || 0;
  const qtyNum = parseFloat(quantity) || 0;
  const total = priceNum * qtyNum;

  const asset = symbol.split('-')[0];
  const isBuy = side === 'BUY';

  // Get available balances
  const availableUsd = balance ? fromScale(balance.usd) : 0;
  const availableBtc = balance ? fromScale(balance.btc) : 0;

  // Validation
  const validation = (() => {
    if (!priceNum || priceNum <= 0) return { valid: false, error: 'Enter a valid price' };
    if (!qtyNum || qtyNum <= 0) return { valid: false, error: 'Enter a valid amount' };
    if (isBuy && total > availableUsd) return { valid: false, error: `Insufficient USD (have $${availableUsd.toFixed(2)})` };
    if (!isBuy && qtyNum > availableBtc) return { valid: false, error: `Insufficient ${asset} (have ${availableBtc.toFixed(4)})` };
    return { valid: true, error: '' };
  })();

  const canSubmit = validation.valid && !loading;

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!canSubmit) {
      setError(validation.error);
      return;
    }

    setLoading(true);
    setError('');
    setSuccess('');

    try {
      const result = await api.place({
        account_id: account,
        symbol,
        side,
        price: toScale(priceNum),
        quantity: toScale(qtyNum),
        time_in_force: tif,
      });

      if (result.order.status === 'REJECTED') {
        setError(result.order.reject_reason || 'Order rejected');
      } else {
        const fills = result.trades.length;
        const fillInfo =
          result.order.status === 'CANCELLED'
            ? fills > 0
              ? `Filled ${fills} trade(s), rest cancelled (${tif})`
              : tif === 'FOK'
                ? 'Not enough liquidity, killed (FOK)'
                : 'Nothing to fill, cancelled (IOC)'
            : fills > 0
              ? `Filled ${fills} trade(s)`
              : 'Resting on book';
        setSuccess(`Order #${result.order.id}: ${fillInfo}`);
        setQuantity('');
        onOrder?.(result.order, result.trades);
        setTimeout(() => setSuccess(''), 3000);
      }
    } catch (e: any) {
      const msg = e.message || 'Order failed';
      setError(msg.includes('SELF_TRADE') ? 'Self-trade prevented' : msg);
    }

    setLoading(false);
  };

  return (
    <div className="card">
      <div className="card-header">Place Order</div>

      <form onSubmit={handleSubmit} className="p-4 space-y-4">
        {/* Side selector */}
        <div className="grid grid-cols-2 gap-2">
          <button
            type="button"
            onClick={() => setSide('BUY')}
            className={`py-3 font-semibold rounded transition-all ${
              isBuy ? 'bg-long text-bg-1' : 'bg-bg-3 text-text-2 hover:text-text-1'
            }`}
          >
            Buy
          </button>
          <button
            type="button"
            onClick={() => setSide('SELL')}
            className={`py-3 font-semibold rounded transition-all ${
              !isBuy ? 'bg-short text-white' : 'bg-bg-3 text-text-2 hover:text-text-1'
            }`}
          >
            Sell
          </button>
        </div>

        {/* Price input */}
        <div>
          <div className="flex justify-between text-[10px] text-text-3 mb-1.5">
            <span>Price</span>
            <span>USD</span>
          </div>
          <div className="relative">
            <span className="absolute left-3 top-1/2 -translate-y-1/2 text-text-3">$</span>
            <input
              type="number"
              step="0.01"
              min="0"
              value={price}
              onChange={e => setPrice(e.target.value)}
              placeholder="0.00"
              className="input pl-7"
              disabled={loading}
            />
          </div>
        </div>

        {/* Quantity input */}
        <div>
          <div className="flex justify-between text-[10px] text-text-3 mb-1.5">
            <span>Amount</span>
            <span>{asset}</span>
          </div>
          <input
            type="number"
            step="0.0001"
            min="0"
            value={quantity}
            onChange={e => setQuantity(e.target.value)}
            placeholder="0.0000"
            className="input"
            disabled={loading}
          />
        </div>

        {/* Quick amounts */}
        <div className="grid grid-cols-4 gap-1.5">
          {[0.1, 0.5, 1, 5].map(amt => (
            <button
              key={amt}
              type="button"
              onClick={() => setQuantity(amt.toString())}
              className="py-1.5 text-xs bg-bg-3 hover:bg-bg-4 rounded text-text-2 transition-colors"
              disabled={loading}
            >
              {amt}
            </button>
          ))}
        </div>

        {/* Time in force */}
        <div>
          <label className="block text-xs text-text-3 mb-1.5">Time in force</label>
          <div className="grid grid-cols-3 gap-1.5">
            {(['GTC', 'IOC', 'FOK'] as const).map(t => (
              <button
                key={t}
                type="button"
                onClick={() => setTif(t)}
                title={
                  t === 'GTC'
                    ? 'Good till cancelled: unfilled size rests on the book'
                    : t === 'IOC'
                      ? 'Immediate or cancel: fill what is there now, cancel the rest'
                      : 'Fill or kill: fill the whole size now or nothing'
                }
                className={`py-1.5 text-xs rounded transition-colors ${
                  tif === t ? 'bg-bg-4 text-text-1' : 'bg-bg-3 text-text-2 hover:bg-bg-4'
                }`}
                disabled={loading}
              >
                {t}
              </button>
            ))}
          </div>
        </div>

        {/* Order summary */}
        {total > 0 && (
          <div className="p-3 bg-bg-1 rounded text-xs space-y-1">
            <div className="flex justify-between">
              <span className="text-text-3">Total</span>
              <span className="mono text-text-1">${total.toFixed(2)}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-text-3">Available</span>
              <span className={`mono ${
                (isBuy && total > availableUsd) || (!isBuy && qtyNum > availableBtc)
                  ? 'text-short'
                  : 'text-text-2'
              }`}>
                {isBuy ? `$${availableUsd.toFixed(2)}` : `${availableBtc.toFixed(4)} ${asset}`}
              </span>
            </div>
          </div>
        )}

        {/* Error message */}
        {error && (
          <div className="p-2 bg-short/10 border border-short/20 rounded text-short text-xs">
            {error}
          </div>
        )}

        {/* Success message */}
        {success && (
          <div className="p-2 bg-long/10 border border-long/20 rounded text-long text-xs">
            {success}
          </div>
        )}

        {/* Submit button */}
        <button
          type="submit"
          disabled={!canSubmit}
          className={`w-full py-3 font-semibold rounded transition-all ${
            isBuy ? 'bg-long text-bg-1' : 'bg-short text-white'
          } disabled:opacity-40 disabled:cursor-not-allowed`}
        >
          {loading ? 'Placing...' : `${side} ${asset}`}
        </button>
      </form>
    </div>
  );
}
