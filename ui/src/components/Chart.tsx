import { useRef, useEffect, useMemo } from 'react';
import { Trade, fromScale } from '../api/client';

interface Props {
  trades: Trade[];
}

export default function Chart({ trades }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  
  const data = useMemo(() => {
    // Need at least 1 trade
    if (trades.length === 0) return null;
    
    // Reverse to get oldest first (for left-to-right time flow)
    const pts = [...trades].reverse().map(t => ({
      price: fromScale(t.price),
      time: t.timestamp_ns,
    }));
    
    const prices = pts.map(p => p.price);
    const min = Math.min(...prices);
    const max = Math.max(...prices);
    
    // Handle single point or same prices - expand range
    let minPrice = min;
    let maxPrice = max;
    if (max - min < 1) {
      const mid = (min + max) / 2 || 50000;
      minPrice = mid - 100;
      maxPrice = mid + 100;
    } else {
      const pad = (max - min) * 0.15;
      minPrice = min - pad;
      maxPrice = max + pad;
    }
    
    return { 
      pts, 
      min: minPrice, 
      max: maxPrice, 
      last: prices[prices.length - 1], 
      first: prices[0],
      high: Math.max(...prices),
      low: Math.min(...prices),
    };
  }, [trades]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container || !data) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Handle high DPI displays
    const rect = container.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    canvas.style.width = rect.width + 'px';
    canvas.style.height = rect.height + 'px';
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    const { pts, min, max, last, first } = data;
    const range = max - min || 1;
    const isUp = last >= first;
    const color = isUp ? '#02c076' : '#f6465d';

    // Clear
    ctx.clearRect(0, 0, w, h);

    // Grid lines
    ctx.strokeStyle = '#2b3139';
    ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const y = (h / 4) * i;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w - 60, y);
      ctx.stroke();
    }

    // Price labels on right
    ctx.fillStyle = '#5e6673';
    ctx.font = '10px IBM Plex Mono';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const price = max - (range * i / 4);
      const y = (h / 4) * i;
      ctx.fillText('$' + price.toLocaleString('en-US', { maximumFractionDigits: 0 }), w - 4, y + 4);
    }

    // Chart area
    const chartW = w - 60;
    const toX = (i: number) => pts.length === 1 ? chartW / 2 : (i / (pts.length - 1)) * chartW;
    const toY = (p: number) => h - ((p - min) / range) * h;

    if (pts.length === 1) {
      // Single point - draw horizontal line and dot
      const y = toY(pts[0].price);
      
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.setLineDash([5, 5]);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(chartW, y);
      ctx.stroke();
      ctx.setLineDash([]);
      
      // Dot
      ctx.beginPath();
      ctx.arc(chartW / 2, y, 6, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
    } else {
      // Multiple points - draw area and line
      
      // Area gradient
      const gradient = ctx.createLinearGradient(0, 0, 0, h);
      gradient.addColorStop(0, isUp ? 'rgba(2, 192, 118, 0.25)' : 'rgba(246, 70, 93, 0.25)');
      gradient.addColorStop(1, 'transparent');

      // Draw area
      ctx.beginPath();
      ctx.moveTo(toX(0), toY(pts[0].price));
      pts.forEach((p, i) => ctx.lineTo(toX(i), toY(p.price)));
      ctx.lineTo(toX(pts.length - 1), h);
      ctx.lineTo(toX(0), h);
      ctx.closePath();
      ctx.fillStyle = gradient;
      ctx.fill();

      // Draw line
      ctx.beginPath();
      ctx.moveTo(toX(0), toY(pts[0].price));
      pts.forEach((p, i) => ctx.lineTo(toX(i), toY(p.price)));
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.stroke();

      // Current price dot
      const lastY = toY(last);
      ctx.beginPath();
      ctx.arc(toX(pts.length - 1), lastY, 4, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();

      // Price line extending to right
      ctx.beginPath();
      ctx.setLineDash([3, 3]);
      ctx.moveTo(toX(pts.length - 1), lastY);
      ctx.lineTo(w - 60, lastY);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1;
      ctx.stroke();
      ctx.setLineDash([]);
    }

    // Current price badge
    const lastY = toY(last);
    ctx.fillStyle = color;
    const badgeY = Math.max(12, Math.min(h - 12, lastY));
    ctx.beginPath();
    ctx.roundRect(w - 58, badgeY - 10, 54, 20, 3);
    ctx.fill();
    ctx.fillStyle = isUp ? '#0b0e11' : '#ffffff';
    ctx.font = '11px IBM Plex Mono';
    ctx.textAlign = 'center';
    ctx.fillText('$' + last.toLocaleString('en-US', { maximumFractionDigits: 0 }), w - 31, badgeY + 4);

  }, [data]);

  if (!data) {
    return (
      <div className="card h-full flex items-center justify-center text-text-3">
        Execute trades to see price chart
      </div>
    );
  }

  const change = data.first > 0 ? ((data.last - data.first) / data.first) * 100 : 0;
  const isUp = change >= 0;

  return (
    <div className="card h-full flex flex-col">
      <div className="card-header">
        <div className="flex items-center gap-3">
          <span>Price Chart</span>
          <span className={`text-sm font-medium ${isUp ? 'text-long' : 'text-short'}`}>
            {isUp ? '+' : ''}{change.toFixed(2)}%
          </span>
        </div>
        <div className="flex gap-3 text-[10px] text-text-3">
          <span>H: <span className="text-text-2 mono">{data.high.toLocaleString()}</span></span>
          <span>L: <span className="text-text-2 mono">{data.low.toLocaleString()}</span></span>
        </div>
      </div>
      <div ref={containerRef} className="flex-1 p-2 min-h-0">
        <canvas ref={canvasRef} className="w-full h-full" />
      </div>
    </div>
  );
}
