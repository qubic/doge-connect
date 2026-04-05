/**
 * Cloudflare Worker for Qubic DOGE Mining Stats
 *
 * KV Binding: stats (namespace), key: DOGE_STATS
 *
 * Routes:
 *   GET /                  -> Stats dashboard HTML
 *   GET /dispatcher.json   -> Dispatcher stats from KV (key: DOGE_STATS)
 *   GET /pool.json         -> Pool stats from KV (key: DOGE_POOL)
 *   GET /favicon.svg       -> Favicon
 */

const FAVICON_SVG = `<svg xmlns="http://www.w3.org/2000/svg" width="31" height="52" fill="none"><path fill="#23FFFF" d="M11.308 0H1.615C.723 0 0 .724 0 1.616v35.538c0 .892.723 1.616 1.615 1.616h9.693c.892 0 1.615-.724 1.615-1.616V1.616C12.923.724 12.2 0 11.308 0Zm17.23 0h-9.692c-.892 0-1.616.723-1.616 1.615v48.462c0 .892.724 1.615 1.616 1.615h9.692c.892 0 1.616-.723 1.616-1.615V1.615C30.154.723 29.43 0 28.538 0Z"/></svg>`;

const HTML_PAGE = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Qubic DOGE Mining Stats</title>
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
        a { color: #60a5fa; text-decoration: none; }
        a:hover { text-decoration: underline; }
        .header {
            display: flex;
            align-items: center;
            gap: 15px;
            margin-bottom: 30px;
        }
        .header h1 { font-size: 1.4em; font-weight: 700; color: white; }
        .header h1 span { color: #f59e0b; }
        .description {
            color: #9ca3af;
            font-size: 0.85em;
            line-height: 1.6;
            max-width: 800px;
            margin-bottom: 30px;
        }
        .status-bar {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 25px;
            font-size: 0.8em;
        }
        .status-dot {
            width: 10px; height: 10px;
            border-radius: 50%;
            background: #22c55e;
            animation: pulse 2s ease-in-out infinite;
        }
        .status-dot.offline { background: #ef4444; animation: none; }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.4; }
        }
        .status-text { color: #9ca3af; }
        .status-text span { color: #22c55e; font-weight: 500; }
        .status-text span.offline { color: #ef4444; }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 15px;
            margin-bottom: 30px;
        }
        .card {
            background: #111d2a;
            border: 1px solid #1e3a50;
            border-radius: 8px;
            padding: 18px;
        }
        .card-label {
            font-size: 0.7em;
            color: #6b7280;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            margin-bottom: 8px;
        }
        .card-value {
            font-size: 1.5em;
            font-weight: 700;
            color: white;
        }
        .card-value.hashrate { color: #f59e0b; }
        .card-value.accepted { color: #22c55e; }
        .card-value.rejected { color: #ef4444; }
        .card-sub {
            font-size: 0.7em;
            color: #6b7280;
            margin-top: 6px;
        }
        .section-title {
            font-size: 0.9em;
            font-weight: 700;
            color: #9ca3af;
            margin-bottom: 15px;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .bar-container {
            background: #111d2a;
            border: 1px solid #1e3a50;
            border-radius: 8px;
            padding: 18px;
            margin-bottom: 30px;
        }
        .bar-row {
            display: flex;
            align-items: center;
            gap: 12px;
            margin-bottom: 10px;
        }
        .bar-row:last-child { margin-bottom: 0; }
        .bar-label {
            font-size: 0.75em;
            color: #9ca3af;
            width: 80px;
            flex-shrink: 0;
        }
        .bar-track {
            flex: 1;
            height: 20px;
            background: #1a2836;
            border-radius: 4px;
            overflow: hidden;
        }
        .bar-fill {
            height: 100%;
            border-radius: 4px;
            transition: width 0.5s ease;
        }
        .bar-fill.green { background: #22c55e; }
        .bar-fill.yellow { background: #f59e0b; }
        .bar-fill.red { background: #ef4444; }
        .bar-fill.gray { background: #4b5563; }
        .bar-value {
            font-size: 0.75em;
            color: #e5e7eb;
            width: 60px;
            text-align: right;
            flex-shrink: 0;
        }
        .footer {
            border-top: 1px solid #1e3a50;
            padding-top: 20px;
            margin-top: 30px;
            color: #4b5563;
            font-size: 0.7em;
            line-height: 1.6;
        }
        .footer a { color: #6b7280; }
        .last-update {
            font-size: 0.7em;
            color: #4b5563;
            margin-bottom: 20px;
        }
        @media (max-width: 600px) {
            .grid { grid-template-columns: 1fr 1fr; }
            .card-value { font-size: 1.2em; }
        }
    </style>
</head>
<body>
    <div class="header">
        <svg xmlns="http://www.w3.org/2000/svg" width="189" height="56" fill="none"><path fill="#23FFFF" d="M12.858 2.155H3.166c-.892 0-1.615.723-1.615 1.615v35.54c0 .892.723 1.615 1.615 1.615h9.692c.893 0 1.616-.723 1.616-1.615V3.77c0-.892-.723-1.615-1.616-1.615Zm17.231-.001h-9.692c-.892 0-1.616.724-1.616 1.616v48.46c0 .892.724 1.616 1.616 1.616h9.692c.892 0 1.615-.724 1.615-1.616V3.77c0-.892-.723-1.616-1.615-1.616Z"/><path fill="#FEF8E8" d="M74.805 54.277V39.325h-.896c-.411.747-1.008 1.475-1.792 2.184-.784.672-1.792 1.232-3.024 1.68-1.195.448-2.688.672-4.48.672-2.315 0-4.443-.56-6.384-1.68-1.941-1.12-3.491-2.725-4.648-4.816-1.158-2.128-1.736-4.685-1.736-7.672v-.84c0-2.987.578-5.525 1.736-7.616 1.194-2.128 2.762-3.752 4.704-4.872 1.941-1.12 4.05-1.68 6.328-1.68 2.688 0 4.741.485 6.16 1.456 1.456.97 2.538 2.072 3.248 3.304h.896v-3.976h5.656v38.808h-5.768Zm-8.568-15.456c2.538 0 4.61-.803 6.216-2.408 1.605-1.605 2.408-3.901 2.408-6.888v-.504c0-2.95-.822-5.227-2.464-6.832-1.606-1.605-3.659-2.408-6.16-2.408-2.464 0-4.517.803-6.16 2.408-1.605 1.605-2.408 3.883-2.408 6.832v.504c0 2.987.803 5.283 2.408 6.888 1.642 1.605 3.696 2.408 6.16 2.408Zm29.49 4.76c-2.09 0-3.957-.467-5.6-1.4-1.642-.933-2.93-2.259-3.863-3.976-.934-1.717-1.4-3.77-1.4-6.16V15.469h5.768v16.184c0 2.39.597 4.163 1.792 5.32 1.194 1.12 2.856 1.68 4.984 1.68 2.352 0 4.237-.784 5.656-2.352 1.456-1.605 2.184-3.901 2.184-6.888V15.469h5.768v27.608h-5.656v-4.144h-.896c-.523 1.12-1.456 2.184-2.8 3.192-1.344.97-3.323 1.456-5.936 1.456Zm35.855.28c-2.688 0-4.76-.485-6.216-1.456-1.418-.97-2.482-2.053-3.192-3.248h-.896v3.92h-5.656v-39.2h5.768v15.344h.896c.448-.747 1.046-1.456 1.792-2.128.747-.71 1.736-1.288 2.968-1.736 1.232-.448 2.744-.672 4.536-.672 2.315 0 4.443.56 6.384 1.68 1.942 1.12 3.491 2.744 4.648 4.872 1.158 2.128 1.736 4.667 1.736 7.616v.84c0 2.987-.597 5.544-1.792 7.672-1.157 2.09-2.706 3.696-4.648 4.816-1.904 1.12-4.013 1.68-6.328 1.68Zm-1.68-5.04c2.502 0 4.555-.803 6.16-2.408 1.643-1.605 2.464-3.901 2.464-6.888v-.504c0-2.95-.802-5.227-2.408-6.832-1.605-1.605-3.677-2.408-6.216-2.408-2.464 0-4.517.803-6.16 2.408-1.605 1.605-2.408 3.883-2.408 6.832v.504c0 2.987.803 5.283 2.408 6.888 1.643 1.605 3.696 2.408 6.16 2.408Zm17.675 4.256V15.469h5.768v27.608h-5.768Zm2.912-31.36c-1.12 0-2.072-.355-2.856-1.064-.747-.747-1.12-1.699-1.12-2.856 0-1.157.373-2.09 1.12-2.8.784-.747 1.736-1.12 2.856-1.12 1.157 0 2.109.373 2.856 1.12.747.71 1.12 1.643 1.12 2.8 0 1.157-.373 2.11-1.12 2.856-.747.71-1.699 1.064-2.856 1.064Zm20.32 32.144c-2.65 0-5.058-.56-7.224-1.68-2.128-1.12-3.826-2.744-5.096-4.872-1.232-2.128-1.848-4.685-1.848-7.672v-.728c0-2.987.616-5.525 1.848-7.616 1.27-2.128 2.968-3.752 5.096-4.872 2.166-1.157 4.574-1.736 7.224-1.736 2.651 0 4.91.485 6.776 1.456 1.867.97 3.36 2.259 4.48 3.864a12.674 12.674 0 0 1 2.24 5.32l-5.6 1.176a9.21 9.21 0 0 0-1.176-3.36c-.597-1.008-1.437-1.81-2.52-2.408-1.082-.597-2.445-.896-4.088-.896-1.605 0-3.061.373-4.368 1.12-1.269.71-2.277 1.755-3.024 3.136-.746 1.344-1.12 2.987-1.12 4.928v.504c0 1.941.374 3.603 1.12 4.984.747 1.381 1.755 2.427 3.024 3.136 1.307.71 2.763 1.064 4.368 1.064 2.427 0 4.275-.616 5.544-1.848 1.27-1.27 2.072-2.875 2.408-4.816l5.6 1.288a14.652 14.652 0 0 1-2.408 5.264c-1.12 1.605-2.613 2.893-4.48 3.864-1.866.933-4.125 1.4-6.776 1.4Z"/></svg>
        <h1>DOGE <span>Mining</span></h1>
    </div>
    <p class="description">
        Qubic's distributed computational power mining Dogecoin.
        Tasks are distributed via the Qubic network to computors, and solutions are submitted to the mining pool.
    </p>

    <div class="bar-container" style="margin-bottom:30px">
        <div style="display:flex;align-items:center;cursor:pointer;gap:8px" onclick="document.getElementById('poolInfo').style.display=document.getElementById('poolInfo').style.display==='none'?'block':'none';this.querySelector('.arrow').textContent=document.getElementById('poolInfo').style.display==='none'?'\u25B6':'\u25BC'">
            <span class="arrow" style="color:#f59e0b;font-size:0.8em">\u25B6</span>
            <span class="section-title" style="margin:0;cursor:pointer">How to Mine DOGE with Qubic</span>
        </div>
        <div id="poolInfo" style="display:none;margin-top:15px">
            <p style="color:#9ca3af;font-size:0.8em;line-height:1.7;margin-bottom:15px">
                Qubic is a decentralized network of 676 computors and its associated miners. Each computor group can contribute its computational power to mine Dogecoin.
                Multiple independent mining pools connect to the Qubic network, and their combined hashrate forms the <strong style="color:#e5e7eb">qubic.org DOGE pool</strong>.
            </p>
            <p style="color:#9ca3af;font-size:0.8em;line-height:1.7;margin-bottom:20px">
                To start mining, you need a <strong style="color:#e5e7eb">Qubic address</strong> as your worker identity or Payout address.
                Create one using any of the <a href="https://qubic.org/#wallets">official wallets</a>.
                For help, join our <a href="https://discord.gg/qubic">Discord server</a>.
            </p>
            <div class="section-title" style="font-size:0.75em;margin-bottom:10px">Available Pools</div>
            <div style="display:grid;gap:12px">
                <div style="background:#0d1a24;border:1px solid #1e3a50;border-radius:6px;padding:14px">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
                        <strong style="color:#f59e0b;font-size:0.9em">apool</strong>
                        <div style="display:flex;gap:10px;font-size:0.7em">
                            <a href="https://apool.top">Website</a>
                            <a href="https://apool.gitbook.io/help/doge-pool-tutorial/doge-mining-tutorial">Setup Guide</a>
                        </div>
                    </div>
                    <div style="font-size:0.85em;color:#6b7280;line-height:2">
                        <div><span style="color:#9ca3af">Main Stratum:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">stratum+tcp://doge.asia.apool.io:3334</code></div>
                        <div><span style="color:#9ca3af">Username:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">your apool subaccount</code></div>
                        <div><span style="color:#9ca3af">Password:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">(empty)</code></div>
                    </div>
                </div>
                <div style="background:#0d1a24;border:1px solid #1e3a50;border-radius:6px;padding:14px">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
                        <strong style="color:#f59e0b;font-size:0.9em">Minerlab</strong>
                        <div style="display:flex;gap:10px;font-size:0.7em">
                            <a href="https://qubic.minerlab.io">Website</a>
                            <a href="https://qubic.minerlab.io">Setup Guide</a>
                        </div>
                    </div>
                    <div style="font-size:0.85em;color:#6b7280;line-height:2">
                        <div><span style="color:#9ca3af">Main Stratum:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">stratum+tcp://qdoge.minerlab.io:18861</code></div>
                        <div><span style="color:#9ca3af">Username:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">MinerlabUsername.workerName</code></div>
                        <div><span style="color:#9ca3af">Password:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">x</code></div>
                    </div>
                </div>
                <div style="background:#0d1a24;border:1px solid #1e3a50;border-radius:6px;padding:14px">
                    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
                        <strong style="color:#f59e0b;font-size:0.9em">QLI (qubic.li)</strong>
                        <div style="display:flex;gap:10px;font-size:0.7em">
                            <a href="https://platform.qubic.li">Website</a>
                            <a href="https://github.com/qubic-li/.github/blob/main/profile/doge-scrypt-on%20qubic.md">Setup Guide</a>
                        </div>
                    </div>
                    <div style="font-size:0.85em;color:#6b7280;line-height:2">
                        <div><span style="color:#9ca3af">Main Stratum:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">stratum+tcp://doge.qubic.li:12480</code></div>
                        <div><span style="color:#9ca3af">Username:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">&lt;QUBIC_ADDRESS&gt;.workerName</code></div>
                        <div><span style="color:#9ca3af">Password:</span> <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">(empty)</code> or e.g. <code style="color:#e5e7eb;background:#111d2a;padding:2px 6px;border-radius:3px">d=153000000</code> to set a starting difficulty</div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <div class="status-bar">
        <div class="status-dot" id="statusDot"></div>
        <div class="status-text">
            Dispatcher: <span id="statusLabel">connecting...</span>
            &bull; Uptime: <span id="uptime" style="color:#e5e7eb">--</span>
        </div>
    </div>
    <div class="last-update">Last update: <span id="lastUpdate">--</span></div>
    <div class="section-title">Mining Overview</div>
    <div class="grid">
        <div class="card">
            <div class="card-label">Hashrate</div>
            <div class="card-value hashrate" id="hashrate">--</div>
        </div>
        <div class="card">
            <div class="card-label">Pool Difficulty</div>
            <div class="card-value" id="difficulty">--</div>
        </div>
        <div class="card">
            <div class="card-label">Tasks Distributed</div>
            <div class="card-value" id="tasks">--</div>
        </div>
        <div class="card">
            <div class="card-label">Active Tasks</div>
            <div class="card-value" id="activeTasks">--</div>
        </div>
        <div class="card">
            <div class="card-label">Active Computors</div>
            <div class="card-value" id="activeComputors">--</div>
            <div class="card-sub">of 676 (last hour)</div>
        </div>
    </div>
    <div class="section-title">Network</div>
    <div class="grid">
        <div class="card">
            <div class="card-label">Connected Peers</div>
            <div class="card-value" id="peers">--</div>
            <div class="card-sub" id="peersSub"></div>
        </div>
        <div class="card">
            <div class="card-label">Solution Queue</div>
            <div class="card-value" id="queueSolutions">--</div>
        </div>
        <div class="card">
            <div class="card-label">Stratum Queue</div>
            <div class="card-value" id="queueStratum">--</div>
        </div>
    </div>
    <div class="section-title">Dispatcher Solutions</div>
    <div class="bar-container">
        <div class="bar-row">
            <div class="bar-label">Received</div>
            <div class="bar-track"><div class="bar-fill gray" id="barReceived" style="width:0%"></div></div>
            <div class="bar-value" id="valReceived">0</div>
        </div>
        <div class="bar-row">
            <div class="bar-label">Accepted</div>
            <div class="bar-track"><div class="bar-fill green" id="barAccepted" style="width:0%"></div></div>
            <div class="bar-value" id="valAccepted">0</div>
        </div>
        <div class="bar-row">
            <div class="bar-label">Rejected</div>
            <div class="bar-track"><div class="bar-fill red" id="barRejected" style="width:0%"></div></div>
            <div class="bar-value" id="valRejected">0</div>
        </div>
        <div class="bar-row">
            <div class="bar-label">Stale</div>
            <div class="bar-track"><div class="bar-fill yellow" id="barStale" style="width:0%"></div></div>
            <div class="bar-value" id="valStale">0</div>
        </div>
    </div>
    <div class="section-title">Pool Shares</div>
    <div class="grid">
        <div class="card">
            <div class="card-label">Submitted</div>
            <div class="card-value" id="poolSubmitted">--</div>
        </div>
        <div class="card">
            <div class="card-label">Accepted</div>
            <div class="card-value accepted" id="poolAccepted">--</div>
        </div>
        <div class="card">
            <div class="card-label">Rejected</div>
            <div class="card-value rejected" id="poolRejected">--</div>
        </div>
        <div class="card">
            <div class="card-label">Acceptance Rate</div>
            <div class="card-value accepted" id="acceptRate">--</div>
        </div>
    </div>
    <div class="section-title">Solo Pool</div>
    <div class="grid">
        <div class="card">
            <div class="card-label">Blocks Found</div>
            <div class="card-value hashrate" id="blocksFound">--</div>
            <div class="card-sub" id="blocksConfirmed"></div>
        </div>
        <div class="card">
            <div class="card-label">Last Block</div>
            <div class="card-value" id="lastBlockHeight" style="font-size:1.1em">--</div>
            <div class="card-sub" id="lastBlockTime"></div>
        </div>
        <div class="card">
            <div class="card-label">Pool Shares</div>
            <div class="card-value accepted" id="poolSharesValid">--</div>
            <div class="card-sub" id="poolSharesInvalid"></div>
        </div>
        <div class="card">
            <div class="card-label">Share Rate</div>
            <div class="card-value" id="shareRate" style="font-size:1.1em">--</div>
            <div class="card-sub">shares/min</div>
        </div>
        <div class="card">
            <div class="card-label">Invalid Rate</div>
            <div class="card-value rejected" id="invalidRate" style="font-size:1.1em">--</div>
        </div>
    </div>
    <div id="recentBlocksSection" style="display:none">
        <div class="section-title">Recent Blocks</div>
        <div class="bar-container" id="recentBlocksList" style="font-size:0.75em; line-height:1.8">
        </div>
    </div>

    <div class="footer">
        <p>Source: <a href="https://github.com/qubic/doge-connect">github.com/qubic/doge-connect</a> | <a href="https://discord.gg/qubic">Discord</a></p>
    </div>
    <script>
        const STATS_URL = '/dispatcher.json';
        const POLL_INTERVAL = 10000;
        function formatUptime(s) {
            const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
            return d > 0 ? d+'d '+h+'h '+m+'m' : h > 0 ? h+'h '+m+'m' : m+'m';
        }
        function formatNumber(n) {
            if (n >= 1e9) return (n/1e9).toFixed(1)+'B';
            if (n >= 1e6) return (n/1e6).toFixed(1)+'M';
            if (n >= 1e3) return (n/1e3).toFixed(1)+'K';
            return n.toString();
        }
        function formatHashrate(h) {
            if (!h || h === 0) return '0 H/s';
            if (h >= 1e12) return (h/1e12).toFixed(1)+' TH/s';
            if (h >= 1e9) return (h/1e9).toFixed(1)+' GH/s';
            if (h >= 1e6) return (h/1e6).toFixed(1)+' MH/s';
            if (h >= 1e3) return (h/1e3).toFixed(1)+' KH/s';
            return h.toFixed(0)+' H/s';
        }
        function setBar(id, v, max) {
            document.getElementById(id).style.width = (max > 0 ? Math.min(100, v/max*100) : 0) + '%';
        }
        let lastOk = false;
        async function fetchStats() {
            try {
                const r = await fetch(STATS_URL + '?t=' + Date.now());
                if (!r.ok) throw new Error(r.status);
                const d = await r.json();
                if (!d.timestamp) throw new Error('empty');
                lastOk = true;
                document.getElementById('statusDot').className = 'status-dot';
                const l = document.getElementById('statusLabel');
                l.textContent = 'online'; l.style.color = '#22c55e';
                document.getElementById('uptime').textContent = formatUptime(d.uptime_seconds);
                document.getElementById('lastUpdate').textContent = new Date(d.timestamp * 1000).toLocaleTimeString();
                document.getElementById('hashrate').textContent = formatHashrate(d.mining.hashrate);
                document.getElementById('difficulty').textContent = formatNumber(d.mining.pool_difficulty);
                document.getElementById('tasks').textContent = formatNumber(d.mining.tasks_distributed);
                document.getElementById('activeTasks').textContent = d.active_tasks;
                document.getElementById('activeComputors').textContent = d.computors_active_1h ?? Object.keys(d.computor_shares || {}).length;
                document.getElementById('peers').textContent = d.network.connected_peers + ' / ' + d.network.total_peers;
                const pp = d.network.total_peers > 0 ? Math.round(d.network.connected_peers/d.network.total_peers*100) : 0;
                document.getElementById('peersSub').textContent = pp + '% connected';
                document.getElementById('queueSolutions').textContent = d.queues.solutions;
                document.getElementById('queueStratum').textContent = d.queues.stratum;
                const mx = Math.max(d.solutions.received, 1);
                document.getElementById('valReceived').textContent = d.solutions.received;
                document.getElementById('valAccepted').textContent = d.solutions.accepted;
                document.getElementById('valRejected').textContent = d.solutions.rejected;
                document.getElementById('valStale').textContent = d.solutions.stale;
                setBar('barReceived', d.solutions.received, mx);
                setBar('barAccepted', d.solutions.accepted, mx);
                setBar('barRejected', d.solutions.rejected, mx);
                setBar('barStale', d.solutions.stale, mx);
                document.getElementById('poolSubmitted').textContent = d.pool.submitted;
                document.getElementById('poolAccepted').textContent = d.pool.accepted;
                document.getElementById('poolRejected').textContent = d.pool.rejected;
                const t = d.pool.accepted + d.pool.rejected;
                document.getElementById('acceptRate').textContent = t > 0 ? (d.pool.accepted/t*100).toFixed(1)+'%' : '--';
            } catch(e) {
                if (lastOk) {
                    document.getElementById('statusDot').className = 'status-dot offline';
                    const l = document.getElementById('statusLabel');
                    l.textContent = 'offline'; l.style.color = '#ef4444';
                    lastOk = false;
                }
            }
        }
        function timeAgo(ts) {
            if (!ts) return '--';
            const ms = typeof ts === 'string' ? new Date(ts).getTime() : (ts > 1e12 ? ts : ts * 1000);
            if (isNaN(ms)) return '--';
            const sec = Math.floor((Date.now() - ms) / 1000);
            if (sec < 0) return 'just now';
            if (sec < 60) return sec + 's ago';
            if (sec < 3600) return Math.floor(sec/60) + 'm ago';
            if (sec < 86400) return Math.floor(sec/3600) + 'h ' + Math.floor((sec%3600)/60) + 'm ago';
            return Math.floor(sec/86400) + 'd ago';
        }

        async function fetchPoolStats() {
            try {
                const r = await fetch('/pool.json?t=' + Date.now());
                if (!r.ok) return;
                const p = await r.json();
                // Support both flat format (sharesValid) and nested format (shares.valid)
                const valid = p.sharesValid ?? (p.shares && p.shares.valid) ?? null;
                const invalid = p.sharesInvalid ?? (p.shares && p.shares.invalid) ?? 0;
                const found = p.blocksFound ?? (p.blocks && p.blocks.found) ?? 0;
                const confirmed = p.blocksConfirmed ?? (p.blocks && p.blocks.confirmed) ?? 0;
                if (valid === null) return;

                document.getElementById('blocksFound').textContent = found;
                document.getElementById('blocksConfirmed').textContent = confirmed + ' confirmed';
                document.getElementById('poolSharesValid').textContent = formatNumber(valid);
                document.getElementById('poolSharesInvalid').textContent = invalid + ' invalid';

                const lastBlock = p.lastBlock || (p.lastBlockHeight ? p : null);
                if (lastBlock && (lastBlock.height || lastBlock.lastBlockHeight)) {
                    const h = lastBlock.height || lastBlock.lastBlockHeight;
                    document.getElementById('lastBlockHeight').textContent = '#' + h.toLocaleString();
                    document.getElementById('lastBlockTime').textContent = timeAgo(lastBlock.time || lastBlock.lastBlockTime || p.lastBlockTime);
                }

                // Share rate: use pool's perMinute if available, otherwise calculate from uptime
                const spm = (p.shares && p.shares.perMinute != null) ? p.shares.perMinute : (p.uptime > 0 ? (valid / p.uptime * 60) : 0);
                document.getElementById('shareRate').textContent = parseFloat(spm).toFixed(1);

                // Invalid rate
                const total = valid + invalid;
                document.getElementById('invalidRate').textContent = total > 0 ? (invalid / total * 100).toFixed(2) + '%' : '0%';

                if (p.recentBlocks && p.recentBlocks.length > 0) {
                    document.getElementById('recentBlocksSection').style.display = '';
                    const list = document.getElementById('recentBlocksList');
                    list.innerHTML = p.recentBlocks.map(b =>
                        '<div style="display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1e3a50">' +
                        '<span style="color:#f59e0b">#' + b.height + '</span>' +
                        '<span style="color:' + (b.confirmed ? '#22c55e' : '#f59e0b') + '">' + (b.confirmed ? 'confirmed' : 'pending') + '</span>' +
                        '<span style="color:#6b7280">' + new Date(b.time).toLocaleString() + '</span>' +
                        '</div>'
                    ).join('');
                }
            } catch(e) {}
        }

        fetchStats();
        fetchPoolStats();
        setInterval(fetchStats, POLL_INTERVAL);
        setInterval(fetchPoolStats, POLL_INTERVAL);
    </script>
</body>
</html>`;

export default {
    async fetch(request, env) {
        const url = new URL(request.url);

        // GET /dispatcher.json -- serve dispatcher stats from KV
        if (url.pathname === '/dispatcher.json') {
            const data = await env.stats.get('DOGE_STATS');
            return new Response(data || '{}', {
                headers: {
                    'Content-Type': 'application/json',
                    'Access-Control-Allow-Origin': '*',
                    'Cache-Control': 'no-cache, max-age=0',
                }
            });
        }

        // GET /pool.json -- serve pool stats from KV
        if (url.pathname === '/pool.json') {
            const data = await env.stats.get('DOGE_POOL');
            return new Response(data || '{}', {
                headers: {
                    'Content-Type': 'application/json',
                    'Access-Control-Allow-Origin': '*',
                    'Cache-Control': 'no-cache, max-age=0',
                }
            });
        }

        // GET /poolstats.json -- combined stats for miningpoolstats.stream
        if (url.pathname === '/poolstats.json') {
            const dispRaw = await env.stats.get('DOGE_STATS');
            const poolRaw = await env.stats.get('DOGE_POOL');
            const disp = dispRaw ? JSON.parse(dispRaw) : {};
            const pool = poolRaw ? JSON.parse(poolRaw) : {};

            const hashrate = disp.mining ? disp.mining.hashrate || 0 : 0;
            const shares = pool.shares ? pool.shares.valid || pool.sharesValid || 0 : pool.sharesValid || 0;
            const blocks = pool.blocks ? pool.blocks.found || pool.blocksFound || 0 : pool.blocksFound || 0;
            const lastBlock = pool.lastBlock || {};
            const blockHeight = lastBlock.height || pool.lastBlockHeight || 0;

            const lastBlockTime = lastBlock.time || pool.lastBlockTime || pool.lastShare || null;
            const lastBlockTs = lastBlockTime
                ? Math.floor((typeof lastBlockTime === 'string' ? new Date(lastBlockTime).getTime() : (lastBlockTime > 1e12 ? lastBlockTime : lastBlockTime * 1000)) / 1000)
                : 0;

            const data = {
                name: "qubic.org",
                dashboard: "https://doge-stats.qubic.org",
                users: 1,
                workers: 1,
                fee: 0,
                minFee: 0,
                maxFee: 0,
                hashrate: hashrate,
                shares: shares,
                blocks: blocks,
                blockHeight: blockHeight,
                lastBlockTime: lastBlockTs,
            };

            return new Response(JSON.stringify(data, null, 2), {
                headers: {
                    'Content-Type': 'application/json',
                    'Access-Control-Allow-Origin': '*',
                    'Cache-Control': 'no-cache, max-age=0',
                }
            });
        }

        // GET /favicon.svg
        if (url.pathname === '/favicon.svg' || url.pathname === '/favicon.ico') {
            return new Response(FAVICON_SVG, {
                headers: {
                    'Content-Type': 'image/svg+xml',
                    'Cache-Control': 'public, max-age=86400',
                }
            });
        }

        // GET / -- serve the dashboard
        if (url.pathname === '/' || url.pathname === '/index.html') {
            return new Response(HTML_PAGE, {
                headers: {
                    'Content-Type': 'text/html;charset=UTF-8',
                    'Cache-Control': 'public, max-age=60',
                }
            });
        }

        // CORS preflight
        if (request.method === 'OPTIONS') {
            return new Response(null, {
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET, OPTIONS',
                    'Access-Control-Allow-Headers': 'Content-Type',
                }
            });
        }

        return new Response('Not Found', { status: 404 });
    }
};
