import { useRef, useEffect, useMemo } from 'react';
import { Level, fromScale } from '../api/client';

interface Props {
  bids: Level[];
  asks: Level[];
}

export default function Depth({ bids, asks }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  const data = useMemo(() => {
    if (bids.length === 0 && asks.length === 0) return null;

    // Sort bids descending, asks ascending
    const sortedBids = [...bids].sort((a, b) => b.price - a.price);
    const sortedAsks = [...asks].sort((a, b) => a.price - b.price);

    // Build cumulative depth
    let bidCumulative = 0;
    let askCumulative = 0;
    
    const bidPoints = sortedBids.slice(0, 50).map(b => ({
      price: fromScale(b.price),
      depth: bidCumulative += fromScale(b.quantity),
    }));
    
    const askPoints = sortedAsks.slice(0, 50).map(a => ({
      price: fromScale(a.price),
      depth: askCumulative += fromScale(a.quantity),
    }));

    // Get price range
    const allPrices = [...bidPoints.map(p => p.price), ...askPoints.map(p => p.price)];
    let minPrice = Math.min(...allPrices);
    let maxPrice = Math.max(...allPrices);
    
    // Handle collapsed range
    if (maxPrice - minPrice < 1) {
      const mid = (minPrice + maxPrice) / 2 || 50000;
      minPrice = mid * 0.995;
      maxPrice = mid * 1.005;
    } else {
      const padding = (maxPrice - minPrice) * 0.05;
      minPrice -= padding;
      maxPrice += padding;
    }

    const maxDepth = Math.max(bidCumulative, askCumulative) || 1;
    
    // Calculate mid price
    const midPrice = sortedBids[0] && sortedAsks[0] 
      ? (fromScale(sortedBids[0].price) + fromScale(sortedAsks[0].price)) / 2 
      : null;

    return { bidPoints, askPoints, minPrice, maxPrice, maxDepth, midPrice };
  }, [bids, asks]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const container = containerRef.current;
    if (!canvas || !container || !data) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const rect = container.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    canvas.style.width = rect.width + 'px';
    canvas.style.height = rect.height + 'px';
    ctx.scale(dpr, dpr);

    const w = rect.width;
    const h = rect.height;
    const { bidPoints, askPoints, minPrice, maxPrice, maxDepth, midPrice } = data;
    const priceRange = maxPrice - minPrice || 1;
    const margin = { top: 15, bottom: 15 };
    const chartH = h - margin.top - margin.bottom;

    ctx.clearRect(0, 0, w, h);

    const toX = (price: number) => ((price - minPrice) / priceRange) * w;
    const toY = (depth: number) => margin.top + chartH - (depth / maxDepth) * chartH;

    // Mid price line
    if (midPrice !== null) {
      ctx.strokeStyle = '#363a45';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.beginPath();
      ctx.moveTo(toX(midPrice), 0);
      ctx.lineTo(toX(midPrice), h);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    // Draw bids (green)
    if (bidPoints.length > 0) {
      const gradient = ctx.createLinearGradient(0, margin.top, 0, h - margin.bottom);
      gradient.addColorStop(0, 'rgba(2, 192, 118, 0.4)');
      gradient.addColorStop(1, 'rgba(2, 192, 118, 0.05)');

      ctx.beginPath();
      ctx.moveTo(toX(bidPoints[0].price), h - margin.bottom);
      
      for (let i = 0; i < bidPoints.length; i++) {
        const x = toX(bidPoints[i].price);
        const y = toY(bidPoints[i].depth);
        if (i === 0) {
          ctx.lineTo(x, y);
        } else {
          ctx.lineTo(x, toY(bidPoints[i - 1].depth));
          ctx.lineTo(x, y);
        }
      }
      
      ctx.lineTo(toX(bidPoints[bidPoints.length - 1].price), h - margin.bottom);
      ctx.closePath();
      ctx.fillStyle = gradient;
      ctx.fill();

      // Line
      ctx.beginPath();
      ctx.moveTo(toX(bidPoints[0].price), toY(bidPoints[0].depth));
      for (let i = 1; i < bidPoints.length; i++) {
        ctx.lineTo(toX(bidPoints[i].price), toY(bidPoints[i - 1].depth));
        ctx.lineTo(toX(bidPoints[i].price), toY(bidPoints[i].depth));
      }
      ctx.strokeStyle = '#02c076';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    // Draw asks (red)
    if (askPoints.length > 0) {
      const gradient = ctx.createLinearGradient(0, margin.top, 0, h - margin.bottom);
      gradient.addColorStop(0, 'rgba(246, 70, 93, 0.4)');
      gradient.addColorStop(1, 'rgba(246, 70, 93, 0.05)');

      ctx.beginPath();
      ctx.moveTo(toX(askPoints[0].price), h - margin.bottom);
      
      for (let i = 0; i < askPoints.length; i++) {
        const x = toX(askPoints[i].price);
        const y = toY(askPoints[i].depth);
        if (i === 0) {
          ctx.lineTo(x, y);
        } else {
          ctx.lineTo(x, toY(askPoints[i - 1].depth));
          ctx.lineTo(x, y);
        }
      }
      
      ctx.lineTo(toX(askPoints[askPoints.length - 1].price), h - margin.bottom);
      ctx.closePath();
      ctx.fillStyle = gradient;
      ctx.fill();

      // Line
      ctx.beginPath();
      ctx.moveTo(toX(askPoints[0].price), toY(askPoints[0].depth));
      for (let i = 1; i < askPoints.length; i++) {
        ctx.lineTo(toX(askPoints[i].price), toY(askPoints[i - 1].depth));
        ctx.lineTo(toX(askPoints[i].price), toY(askPoints[i].depth));
      }
      ctx.strokeStyle = '#f6465d';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    // Price labels
    ctx.fillStyle = '#5e6673';
    ctx.font = '9px IBM Plex Mono';
    ctx.textAlign = 'left';
    ctx.fillText('$' + minPrice.toLocaleString('en-US', { maximumFractionDigits: 0 }), 4, h - 3);
    ctx.textAlign = 'right';
    ctx.fillText('$' + maxPrice.toLocaleString('en-US', { maximumFractionDigits: 0 }), w - 4, h - 3);
    
    if (midPrice !== null) {
      ctx.textAlign = 'center';
      ctx.fillText('$' + midPrice.toLocaleString('en-US', { maximumFractionDigits: 0 }), toX(midPrice), 10);
    }
  }, [data]);

  if (!data) return null;

  return (
    <div className="card h-full flex flex-col">
      <div className="card-header">
        <span>Market Depth</span>
        <div className="flex gap-3 text-[10px] font-normal">
          {data.bidPoints.length > 0 && <span className="text-long">● Bids</span>}
          {data.askPoints.length > 0 && <span className="text-short">● Asks</span>}
        </div>
      </div>
      <div ref={containerRef} className="flex-1 p-1 min-h-0">
        <canvas ref={canvasRef} className="w-full h-full" />
      </div>
    </div>
  );
}
