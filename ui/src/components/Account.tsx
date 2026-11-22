import { User, Wallet, RefreshCw } from 'lucide-react';
import { Balance, fromScale } from '../api/client';

interface Props {
  account: string;
  accounts: string[];
  balance: Balance | null;
  onChange: (account: string) => void;
  onRefresh?: () => void;
}

export default function Account({ account, accounts, balance, onChange, onRefresh }: Props) {
  // Convert from fixed-point to display values
  const usdAvailable = balance ? fromScale(balance.usd) : 0;
  const btcAvailable = balance ? fromScale(balance.btc) : 0;
  const usdReserved = balance ? fromScale(balance.usd_reserved) : 0;
  const btcReserved = balance ? fromScale(balance.btc_reserved) : 0;

  return (
    <div className="card">
      <div className="card-header">
        <span className="flex items-center gap-1.5">
          <User size={12} />
          Account
        </span>
        {onRefresh && (
          <button 
            onClick={onRefresh}
            className="p-1 text-text-3 hover:text-text-1 transition-colors"
            title="Refresh balances"
          >
            <RefreshCw size={12} />
          </button>
        )}
      </div>
      <div className="p-4 space-y-4">
        {/* Account selector */}
        <div className="grid grid-cols-2 gap-2">
          {accounts.map(a => (
            <button
              key={a}
              onClick={() => onChange(a)}
              className={`py-2.5 text-sm font-medium rounded transition-all ${
                a === account
                  ? 'bg-text-1 text-bg-1'
                  : 'bg-bg-3 text-text-2 hover:text-text-1'
              }`}
            >
              {a}
            </button>
          ))}
        </div>

        {/* Balances */}
        <div className="p-3 bg-bg-1 rounded space-y-2">
          <div className="flex items-center gap-1.5 text-[10px] text-text-3 mb-2">
            <Wallet size={10} />
            <span>Balances</span>
          </div>
          
          {/* USD */}
          <div className="flex justify-between text-xs">
            <span className="text-text-2">USD Available</span>
            <span className="mono text-text-1 font-medium">
              ${usdAvailable.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
            </span>
          </div>
          {usdReserved > 0 && (
            <div className="flex justify-between text-[10px]">
              <span className="text-text-3">Reserved (open orders)</span>
              <span className="mono text-text-3">
                ${usdReserved.toLocaleString('en-US', { minimumFractionDigits: 2 })}
              </span>
            </div>
          )}

          {/* BTC */}
          <div className="flex justify-between text-xs">
            <span className="text-text-2">BTC Available</span>
            <span className="mono text-text-1 font-medium">
              {btcAvailable.toLocaleString('en-US', { minimumFractionDigits: 4, maximumFractionDigits: 8 })}
            </span>
          </div>
          {btcReserved > 0 && (
            <div className="flex justify-between text-[10px]">
              <span className="text-text-3">Reserved (open orders)</span>
              <span className="mono text-text-3">
                {btcReserved.toLocaleString('en-US', { minimumFractionDigits: 4 })}
              </span>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
