import { useState } from 'react';
import { X, Clock, CheckCircle, AlertCircle } from 'lucide-react';
import { Order, api, fmt } from '../api/client';

interface Props {
  orders: Order[];
  onCancel?: (order: Order) => void;
}

const STATUS_CONFIG: Record<string, { label: string; color: string; Icon: any }> = {
  NEW: { label: 'Open', color: 'text-text-2', Icon: Clock },
  PARTIAL: { label: 'Partial', color: 'text-long', Icon: Clock },
  FILLED: { label: 'Filled', color: 'text-long', Icon: CheckCircle },
  CANCELLED: { label: 'Cancelled', color: 'text-text-3', Icon: X },
  REJECTED: { label: 'Rejected', color: 'text-short', Icon: AlertCircle },
};

export default function OpenOrders({ orders, onCancel }: Props) {
  const [cancelling, setCancelling] = useState<number | null>(null);

  const sortedOrders = [...orders].sort((a, b) => b.timestamp_ns - a.timestamp_ns);
  const openOrders = sortedOrders.filter(o => o.status === 'NEW' || o.status === 'PARTIAL');
  const closedOrders = sortedOrders.filter(o => o.status !== 'NEW' && o.status !== 'PARTIAL').slice(0, 5);

  const handleCancel = async (orderId: number) => {
    setCancelling(orderId);
    try {
      const result = await api.cancel(orderId);
      onCancel?.(result);
    } catch (e) {
      console.error('Cancel failed:', e);
    }
    setCancelling(null);
  };

  const handleCancelAll = () => {
    openOrders.forEach(o => handleCancel(o.id));
  };

  return (
    <div className="card h-full flex flex-col">
      <div className="card-header">
        <span>Orders ({openOrders.length})</span>
        {openOrders.length > 1 && (
          <button
            onClick={handleCancelAll}
            className="text-[10px] text-short hover:text-short/80 transition-colors"
          >
            Cancel All
          </button>
        )}
      </div>

      <div className="flex-1 overflow-y-auto">
        {sortedOrders.length === 0 ? (
          <div className="p-6 text-center text-text-3 text-sm">
            No orders yet
          </div>
        ) : (
          <>
            {/* Open orders */}
            {openOrders.map(order => (
              <OrderRow
                key={order.id}
                order={order}
                onCancel={() => handleCancel(order.id)}
                isCancelling={cancelling === order.id}
              />
            ))}
            
            {/* History divider */}
            {closedOrders.length > 0 && openOrders.length > 0 && (
              <div className="px-3 py-2 text-[10px] text-text-3 border-t border-border bg-bg-1">
                History
              </div>
            )}
            
            {/* Closed orders */}
            {closedOrders.map(order => (
              <OrderRow
                key={order.id}
                order={order}
                onCancel={() => {}}
                isCancelling={false}
                isHistory
              />
            ))}
          </>
        )}
      </div>
    </div>
  );
}

interface OrderRowProps {
  order: Order;
  onCancel: () => void;
  isCancelling: boolean;
  isHistory?: boolean;
}

function OrderRow({ order, onCancel, isCancelling, isHistory }: OrderRowProps) {
  const config = STATUS_CONFIG[order.status] || STATUS_CONFIG.NEW;
  const filledQty = order.quantity - order.remaining_qty;
  const fillPct = (filledQty / order.quantity) * 100;
  const canCancel = order.status === 'NEW' || order.status === 'PARTIAL';

  return (
    <div className={`p-3 border-b border-border ${isHistory ? 'opacity-60' : 'hover:bg-bg-3'} transition-colors`}>
      {/* Header */}
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-2">
          <span className={`text-xs font-semibold ${order.side === 'BUY' ? 'text-long' : 'text-short'}`}>
            {order.side}
          </span>
          <span className="text-[10px] text-text-3">#{order.id}</span>
        </div>
        <div className="flex items-center gap-2">
          <div className={`flex items-center gap-1 text-[10px] ${config.color}`}>
            <config.Icon size={10} />
            {config.label}
          </div>
          {canCancel && !isHistory && (
            <button
              onClick={onCancel}
              disabled={isCancelling}
              className="p-1 text-text-3 hover:text-short transition-colors disabled:opacity-50"
            >
              {isCancelling ? '...' : <X size={12} />}
            </button>
          )}
        </div>
      </div>

      {/* Details */}
      <div className="grid grid-cols-2 gap-1 text-xs">
        <div className="flex justify-between">
          <span className="text-text-3">Price</span>
          <span className="mono">{fmt.price(order.price)}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-text-3">Size</span>
          <span className="mono">{fmt.qty(order.quantity)}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-text-3">Filled</span>
          <span className="mono">{fillPct.toFixed(0)}%</span>
        </div>
        <div className="flex justify-between">
          <span className="text-text-3">Time</span>
          <span>{fmt.time(order.timestamp_ns)}</span>
        </div>
      </div>

      {/* Fill progress bar */}
      {fillPct > 0 && fillPct < 100 && (
        <div className="mt-2 h-1 bg-bg-1 rounded-full overflow-hidden">
          <div 
            className="h-full bg-long transition-all"
            style={{ width: `${fillPct}%` }}
          />
        </div>
      )}
    </div>
  );
}
