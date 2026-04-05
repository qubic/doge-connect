namespace Qubic.Doge.Relay;

public static class DemoPage
{
    public const string FaviconSvg = """<svg xmlns="http://www.w3.org/2000/svg" width="31" height="52" fill="none"><path fill="#23FFFF" d="M11.308 0H1.615C.723 0 0 .724 0 1.616v35.538c0 .892.723 1.616 1.615 1.616h9.693c.892 0 1.615-.724 1.615-1.616V1.616C12.923.724 12.2 0 11.308 0Zm17.23 0h-9.692c-.892 0-1.616.723-1.616 1.615v48.462c0 .892.724 1.615 1.616 1.615h9.692c.892 0 1.616-.723 1.616-1.615V1.615C30.154.723 29.43 0 28.538 0Z"/></svg>""";

    public const string Html = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Qubic DOGE Live Feed</title>
    <link rel="icon" type="image/svg+xml" href="/favicon.svg">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Roboto+Mono:wght@400;500;700&display=swap" rel="stylesheet">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            background: #0A1721;
            color: #e5e7eb;
            font-family: 'Roboto Mono', monospace;
            padding: 20px;
            min-height: 100vh;
        }
        .header { display: flex; align-items: center; gap: 15px; margin-bottom: 5px; }
        h1 { font-size: 1.4em; font-weight: 700; color: white; }
        h1 span { color: #f59e0b; }
        .sub { color: #9ca3af; font-size: 0.8em; margin-bottom: 20px; }
        .status-bar {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 20px;
            font-size: 0.8em;
        }
        .status-dot {
            width: 10px; height: 10px;
            border-radius: 50%;
            background: #22c55e;
            animation: pulse 2s ease-in-out infinite;
        }
        .status-dot.offline { background: #ef4444; animation: none; }
        @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.4; } }
        .status-text { color: #9ca3af; }
        .status-text span { color: #22c55e; font-weight: 500; }
        .status-text span.offline { color: #ef4444; }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
            gap: 10px;
            margin-bottom: 20px;
        }
        .stat {
            background: #111d2a;
            border: 1px solid #1e3a50;
            border-radius: 6px;
            padding: 12px;
        }
        .stat-label { font-size: 0.65em; color: #6b7280; text-transform: uppercase; letter-spacing: 0.05em; }
        .stat-value { font-size: 1.3em; font-weight: 700; color: white; margin-top: 4px; }
        .stat-value.valid { color: #22c55e; }
        .stat-value.invalid { color: #ef4444; }
        .stat-value.block { color: #f59e0b; }
        .feed {
            background: #111d2a;
            border: 1px solid #1e3a50;
            border-radius: 8px;
            overflow: hidden;
        }
        .feed-header {
            background: #0d1a24;
            padding: 10px 15px;
            font-size: 0.7em;
            color: #9ca3af;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            border-bottom: 1px solid #1e3a50;
        }
        .feed-list {
            max-height: 70vh;
            overflow-y: auto;
            font-size: 0.75em;
        }
        .feed-row {
            padding: 8px 15px;
            border-bottom: 1px solid #1a2836;
            display: grid;
            grid-template-columns: 60px 80px 100px 100px 100px 40px 1fr;
            gap: 10px;
            align-items: center;
            animation: slideIn 0.3s ease-out;
        }
        .feed-header-row {
            padding: 8px 15px;
            display: grid;
            grid-template-columns: 60px 80px 100px 100px 100px 40px 1fr;
            gap: 10px;
            font-size: 0.65em;
            color: #6b7280;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            border-bottom: 1px solid #1e3a50;
            background: #0d1a24;
        }
        .height-col { color: #6b7280; text-align: right; }
        .hash-col { color: #6b7280; font-family: ui-monospace, monospace; }
        .block-flag { text-align: center; }
        .block-flag.yes { color: #f59e0b; font-weight: 700; }
        @keyframes slideIn {
            from { opacity: 0; transform: translateY(-5px); background: #1e3a50; }
            to { opacity: 1; transform: translateY(0); background: transparent; }
        }
        .feed-row.block { background: rgba(245, 158, 11, 0.1); border-left: 3px solid #f59e0b; }
        .feed-row.invalid { opacity: 0.6; }
        .type-share { color: #9ca3af; }
        .type-block { color: #f59e0b; font-weight: 700; }
        .status-valid { color: #22c55e; }
        .status-invalid { color: #ef4444; }
        .address { color: #e5e7eb; font-family: ui-monospace, monospace; }
        .diff { color: #6b7280; text-align: right; }
        .time { color: #6b7280; text-align: right; font-size: 0.85em; }
        @media (max-width: 700px) {
            .feed-row { grid-template-columns: 1fr 1fr; gap: 4px; font-size: 0.7em; }
            .feed-row .diff, .feed-row .time { grid-column: span 1; }
        }
    </style>
</head>
<body>
    <div class="header">
        <svg xmlns="http://www.w3.org/2000/svg" width="189" height="56" fill="none"><path fill="#23FFFF" d="M12.858 2.155H3.166c-.892 0-1.615.723-1.615 1.615v35.54c0 .892.723 1.615 1.615 1.615h9.692c.893 0 1.616-.723 1.616-1.615V3.77c0-.892-.723-1.615-1.616-1.615Zm17.231-.001h-9.692c-.892 0-1.616.724-1.616 1.616v48.46c0 .892.724 1.616 1.616 1.616h9.692c.892 0 1.615-.724 1.615-1.616V3.77c0-.892-.723-1.616-1.615-1.616Z"/><path fill="#FEF8E8" d="M74.805 54.277V39.325h-.896c-.411.747-1.008 1.475-1.792 2.184-.784.672-1.792 1.232-3.024 1.68-1.195.448-2.688.672-4.48.672-2.315 0-4.443-.56-6.384-1.68-1.941-1.12-3.491-2.725-4.648-4.816-1.158-2.128-1.736-4.685-1.736-7.672v-.84c0-2.987.578-5.525 1.736-7.616 1.194-2.128 2.762-3.752 4.704-4.872 1.941-1.12 4.05-1.68 6.328-1.68 2.688 0 4.741.485 6.16 1.456 1.456.97 2.538 2.072 3.248 3.304h.896v-3.976h5.656v38.808h-5.768Zm-8.568-15.456c2.538 0 4.61-.803 6.216-2.408 1.605-1.605 2.408-3.901 2.408-6.888v-.504c0-2.95-.822-5.227-2.464-6.832-1.606-1.605-3.659-2.408-6.16-2.408-2.464 0-4.517.803-6.16 2.408-1.605 1.605-2.408 3.883-2.408 6.832v.504c0 2.987.803 5.283 2.408 6.888 1.642 1.605 3.696 2.408 6.16 2.408Zm29.49 4.76c-2.09 0-3.957-.467-5.6-1.4-1.642-.933-2.93-2.259-3.863-3.976-.934-1.717-1.4-3.77-1.4-6.16V15.469h5.768v16.184c0 2.39.597 4.163 1.792 5.32 1.194 1.12 2.856 1.68 4.984 1.68 2.352 0 4.237-.784 5.656-2.352 1.456-1.605 2.184-3.901 2.184-6.888V15.469h5.768v27.608h-5.656v-4.144h-.896c-.523 1.12-1.456 2.184-2.8 3.192-1.344.97-3.323 1.456-5.936 1.456Zm35.855.28c-2.688 0-4.76-.485-6.216-1.456-1.418-.97-2.482-2.053-3.192-3.248h-.896v3.92h-5.656v-39.2h5.768v15.344h.896c.448-.747 1.046-1.456 1.792-2.128.747-.71 1.736-1.288 2.968-1.736 1.232-.448 2.744-.672 4.536-.672 2.315 0 4.443.56 6.384 1.68 1.942 1.12 3.491 2.744 4.648 4.872 1.158 2.128 1.736 4.667 1.736 7.616v.84c0 2.987-.597 5.544-1.792 7.672-1.157 2.09-2.706 3.696-4.648 4.816-1.904 1.12-4.013 1.68-6.328 1.68Zm-1.68-5.04c2.502 0 4.555-.803 6.16-2.408 1.643-1.605 2.464-3.901 2.464-6.888v-.504c0-2.95-.802-5.227-2.408-6.832-1.605-1.605-3.677-2.408-6.216-2.408-2.464 0-4.517.803-6.16 2.408-1.605 1.605-2.408 3.883-2.408 6.832v.504c0 2.987.803 5.283 2.408 6.888 1.643 1.605 3.696 2.408 6.16 2.408Zm17.675 4.256V15.469h5.768v27.608h-5.768Zm2.912-31.36c-1.12 0-2.072-.355-2.856-1.064-.747-.747-1.12-1.699-1.12-2.856 0-1.157.373-2.09 1.12-2.8.784-.747 1.736-1.12 2.856-1.12 1.157 0 2.109.373 2.856 1.12.747.71 1.12 1.643 1.12 2.8 0 1.157-.373 2.11-1.12 2.856-.747.71-1.699 1.064-2.856 1.064Zm20.32 32.144c-2.65 0-5.058-.56-7.224-1.68-2.128-1.12-3.826-2.744-5.096-4.872-1.232-2.128-1.848-4.685-1.848-7.672v-.728c0-2.987.616-5.525 1.848-7.616 1.27-2.128 2.968-3.752 5.096-4.872 2.166-1.157 4.574-1.736 7.224-1.736 2.651 0 4.91.485 6.776 1.456 1.867.97 3.36 2.259 4.48 3.864a12.674 12.674 0 0 1 2.24 5.32l-5.6 1.176a9.21 9.21 0 0 0-1.176-3.36c-.597-1.008-1.437-1.81-2.52-2.408-1.082-.597-2.445-.896-4.088-.896-1.605 0-3.061.373-4.368 1.12-1.269.71-2.277 1.755-3.024 3.136-.746 1.344-1.12 2.987-1.12 4.928v.504c0 1.941.374 3.603 1.12 4.984.747 1.381 1.755 2.427 3.024 3.136 1.307.71 2.763 1.064 4.368 1.064 2.427 0 4.275-.616 5.544-1.848 1.27-1.27 2.072-2.875 2.408-4.816l5.6 1.288a14.652 14.652 0 0 1-2.408 5.264c-1.12 1.605-2.613 2.893-4.48 3.864-1.866.933-4.125 1.4-6.776 1.4Z"/></svg>
        <h1>DOGE <span>Live Feed</span></h1>
    </div>
    <div class="sub">Real-time share stream from the qubic.org DOGE pool</div>
    <div class="status-bar">
        <div class="status-dot" id="statusDot"></div>
        <div class="status-text">Status: <span id="statusLabel">connecting...</span></div>
    </div>
    <div class="stats">
        <div class="stat"><div class="stat-label">Shares (valid)</div><div class="stat-value valid" id="countValid">0</div></div>
        <div class="stat"><div class="stat-label">Shares (invalid)</div><div class="stat-value invalid" id="countInvalid">0</div></div>
        <div class="stat"><div class="stat-label">Blocks</div><div class="stat-value block" id="countBlocks">0</div></div>
        <div class="stat"><div class="stat-label">Rate (1min)</div><div class="stat-value" id="rate">0</div></div>
        <div class="stat"><div class="stat-label">Avg Share Diff</div><div class="stat-value" id="avgShareDiff">-</div></div>
    </div>
    <div class="feed">
        <div class="feed-header">Live Events</div>
        <div class="feed-header-row">
            <span>Type</span>
            <span>Status</span>
            <span style="text-align:right">Pool Diff</span>
            <span style="text-align:right">Share Diff</span>
            <span style="text-align:right">Height</span>
            <span style="text-align:center">Blk</span>
            <span style="text-align:right">Time</span>
        </div>
        <div class="feed-list" id="feed"></div>
    </div>
    <script>
        let countValid = 0, countInvalid = 0, countBlocks = 0;
        let shareDiffSum = 0, shareDiffCount = 0;
        const timestamps = [];
        const MAX_ROWS = 200;
        const feed = document.getElementById('feed');

        function formatTime(ts) {
            const d = new Date(ts);
            return d.toLocaleTimeString();
        }

        function formatNumber(n) {
            n = Number(n);
            if (!Number.isFinite(n)) return '-';
            if (n >= 1e9) return (n/1e9).toFixed(1)+'B';
            if (n >= 1e6) return (n/1e6).toFixed(1)+'M';
            if (n >= 1e3) return (n/1e3).toFixed(1)+'K';
            return Math.round(n).toString();
        }

        function addEvent(evt) {
            const row = document.createElement('div');
            const isBlockEvent = evt.type === 'block';
            const isInvalid = evt.valid === false;
            row.className = 'feed-row' + (isBlockEvent || evt.isBlock ? ' block' : '') + (isInvalid ? ' invalid' : '');

            if (isBlockEvent) {
                row.innerHTML =
                    '<span class="type-block">BLOCK</span>' +
                    '<span class="status-valid">' + (evt.confirmed ? 'confirmed' : 'pending') + '</span>' +
                    '<span class="diff">-</span>' +
                    '<span class="diff">-</span>' +
                    '<span class="height-col">' + (evt.height || '-') + '</span>' +
                    '<span class="block-flag yes">Y</span>' +
                    '<span class="time">' + formatTime(evt.ts) + '</span>';
                countBlocks++;
                document.getElementById('countBlocks').textContent = countBlocks;
            } else {
                const status = isInvalid ? 'rejected' : 'accepted';
                const statusClass = isInvalid ? 'status-invalid' : 'status-valid';
                const blkFlag = evt.isBlock ? '<span class="block-flag yes">Y</span>' : '<span class="block-flag">-</span>';
                row.innerHTML =
                    '<span class="type-share">share</span>' +
                    '<span class="' + statusClass + '">' + status + '</span>' +
                    '<span class="diff">' + formatNumber(evt.difficulty) + '</span>' +
                    '<span class="diff">' + formatNumber(evt.shareDiff) + '</span>' +
                    '<span class="height-col">' + (evt.height || '-') + '</span>' +
                    blkFlag +
                    '<span class="time">' + formatTime(evt.ts) + '</span>';
                if (isInvalid) {
                    countInvalid++;
                    document.getElementById('countInvalid').textContent = countInvalid;
                } else {
                    countValid++;
                    timestamps.push(Date.now());
                    document.getElementById('countValid').textContent = countValid;
                    const sd = Number(evt.shareDiff);
                    if (Number.isFinite(sd) && sd > 0) {
                        shareDiffSum += sd;
                        shareDiffCount++;
                        const avg = shareDiffSum / shareDiffCount;
                        document.getElementById('avgShareDiff').textContent = formatNumber(Math.round(avg));
                    }
                }
            }

            feed.insertBefore(row, feed.firstChild);
            while (feed.children.length > MAX_ROWS)
                feed.removeChild(feed.lastChild);
        }

        // Update rate every second (shares in last 60s).
        setInterval(() => {
            const cutoff = Date.now() - 60000;
            while (timestamps.length && timestamps[0] < cutoff) timestamps.shift();
            document.getElementById('rate').textContent = timestamps.length + '/min';
        }, 1000);

        let ws;
        let reconnectTimer = null;
        let userClosed = false; // true when we intentionally closed (tab hidden)

        function setStatus(text, offline) {
            document.getElementById('statusDot').className = 'status-dot' + (offline ? ' offline' : '');
            const l = document.getElementById('statusLabel');
            l.textContent = text;
            l.className = offline ? 'offline' : '';
        }

        function connect() {
            if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
            userClosed = false;
            const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
            ws = new WebSocket(proto + '//' + location.host + '/ws');
            ws.onopen = () => setStatus('connected', false);
            ws.onmessage = (e) => {
                try { addEvent(JSON.parse(e.data)); } catch (err) {}
            };
            ws.onclose = () => {
                if (userClosed) {
                    setStatus('paused (tab hidden)', true);
                    return;
                }
                setStatus('disconnected — reconnecting...', true);
                reconnectTimer = setTimeout(connect, 3000);
            };
            ws.onerror = () => { try { ws.close(); } catch(e) {} };
        }

        function disconnect() {
            userClosed = true;
            if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
            if (ws) { try { ws.close(); } catch(e) {} }
        }

        // Pause WebSocket when tab is hidden, resume when visible.
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                disconnect();
            } else {
                connect();
            }
        });

        connect();
    </script>
</body>
</html>
""";
}
