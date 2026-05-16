const Text = require('../../locales/index');
const http = require('http');
const fs = require('fs');
const path = require('path');

////////////////////////////////////////////////////////////////////////////////

// Main Stratum Function
const Stratum = function (logger, config, configMain) {

  const _this = this;
  this.logger = logger;
  this.config = config;
  this.configMain = configMain;
  this.text = Text[configMain.language];

  // Stratum Variables
  process.setMaxListeners(0);
  this.forkId = process.env.forkId;
  this.statsFile = configMain.statsFile || path.join(require('os').homedir(), '.foundation/stats.json');

  // Load stats from disk
  this.loadStats = function() {
    try {
      if (fs.existsSync(_this.statsFile)) {
        const data = JSON.parse(fs.readFileSync(_this.statsFile, 'utf8'));
        data.startTime = Date.now();
        if (!data.recentBlocks) data.recentBlocks = [];
        if (!data.workers) data.workers = {};
        return data;
      }
    } catch (e) {
      _this.logger && _this.logger['warning']('Pool', 'Stats', [`Failed to load stats: ${e.message}`]);
    }
    return {
      startTime: Date.now(),
      sharesValid: 0,
      sharesInvalid: 0,
      blocksFound: 0,            // primary chain (LTC for merged, DOGE for solo)
      blocksConfirmed: 0,
      blocksFoundAuxiliary: 0,   // auxiliary chain (DOGE for merged) — 0 on solo instances
      blocksConfirmedAuxiliary: 0,
      lastShareTime: null,
      lastBlockTime: null,
      lastBlockHeight: null,
      lastBlockAuxiliaryTime: null,
      lastBlockAuxiliaryHeight: null,
      recentBlocks: [],          // each entry tagged with chain + coin
      workers: {},
    };
  };

  // Save stats to disk
  this.saveStats = function() {
    try {
      const dir = path.dirname(_this.statsFile);
      if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(_this.statsFile, JSON.stringify(_this.stats, null, 2));
    } catch (e) {
      _this.logger['warning']('Pool', 'Stats', [`Failed to save stats: ${e.message}`]);
    }
  };

  // Pool Statistics — load from disk or start fresh
  this.stats = _this.loadStats();

  // Rolling shares-per-minute tracker (not persisted — live metric only)
  this.shareTimestamps = [];

  // Send stats event to master process for aggregation
  this.sendStatsEvent = function(event) {
    try {
      if (process.send) process.send({ type: 'stats', event: event });
    } catch (e) {}
  };

  // Redis publisher for live share feed (optional — only connects if configured).
  // Safety: fully isolated from share processing. All operations are fire-and-forget
  // with no awaits. Errors are silently dropped so Redis outages cannot affect the pool.
  this.redisPublisher = null;
  this.redisErrorLogged = false;
  this.redisDropCount = 0;
  // Only the stats aggregator process publishes (not the per-fork pool workers).
  // Workers have forkId=0,1,2... The aggregator (instantiated in builder.js) has no forkId.
  const isMasterProcess = process.env.forkId === undefined;
  if (isMasterProcess && configMain.redis && configMain.redis.url) {
    try {
      const redis = require('redis');
      _this.redisPublisher = redis.createClient({
        url: configMain.redis.url,
        // Disable built-in command queueing when offline — we don't want unbounded memory growth.
        disableOfflineQueue: true,
        socket: {
          reconnectStrategy: (retries) => Math.min(retries * 200, 5000),
          connectTimeout: 5000,
        }
      });
      _this.redisPublisher.on('error', (err) => {
        // Throttle error logging to avoid log spam (Redis down = many errors).
        if (!_this.redisErrorLogged) {
          _this.redisErrorLogged = true;
          _this.logger && _this.logger['warning']('Pool', 'Redis', [`Publisher error: ${err.message}`]);
          setTimeout(() => { _this.redisErrorLogged = false; }, 60000);
        }
      });
      _this.redisPublisher.on('ready', () => {
        _this.logger['log']('Pool', 'Redis', [`Live feed publisher READY on ${configMain.redis.url}`]);
      });
      _this.redisPublisher.on('connect', () => {
        _this.logger['log']('Pool', 'Redis', [`Socket connected`]);
      });
      _this.redisPublisher.on('end', () => {
        _this.logger['warning']('Pool', 'Redis', [`Connection closed`]);
      });
      _this.redisPublisher.on('reconnecting', () => {
        _this.logger['log']('Pool', 'Redis', [`Reconnecting...`]);
      });
      _this.logger['log']('Pool', 'Redis', [`Connecting to ${configMain.redis.url}...`]);
      // Fire and forget — don't block startup on Redis availability.
      _this.redisPublisher.connect().catch((e) => {
        _this.logger['warning']('Pool', 'Redis', [`Initial connect failed: ${e.message}`]);
      });
    } catch (e) {
      _this.logger && _this.logger['warning']('Pool', 'Redis', [`Failed to init publisher: ${e.message}`]);
      _this.redisPublisher = null;
    }
  }

  // Periodic log of publish stats (master only, once a minute).
  if (isMasterProcess && configMain.redis && configMain.redis.url) {
    _this.redisPublishCount = 0;
    setInterval(() => {
      _this.logger['log']('Pool', 'Redis', [
        `Stats: ready=${_this.redisPublisher ? _this.redisPublisher.isReady : false} published=${_this.redisPublishCount} dropped=${_this.redisDropCount}`
      ]);
    }, 60000);
  }

  // Publish an event — strictly non-blocking, errors silently dropped.
  this.publishLiveEvent = function(event) {
    if (!_this.redisPublisher || !_this.redisPublisher.isReady) {
      _this.redisDropCount++;
      return;
    }
    let payload;
    try {
      payload = JSON.stringify(event);
    } catch (e) {
      return; // bad event, drop
    }
    try {
      const channel = (configMain.redis && configMain.redis.channel) || 'doge:shares';
      // fire-and-forget: node-redis returns a Promise but we don't await it.
      _this.redisPublisher.publish(channel, payload)
        .then(() => { _this.redisPublishCount = (_this.redisPublishCount || 0) + 1; })
        .catch(() => { _this.redisDropCount++; });
    } catch (e) {
      // Synchronous error from publish() — drop silently.
      _this.redisDropCount++;
    }
  };

  // Build Stratum from Configuration
  this.handleStratum = function(callback) {

    // Build Stratum Server
    const Pool = require('../../stratum/main/pool');
    _this.stratum = new Pool(_this.config, _this.configMain, callback);

    // Handle Stratum Main Events
    _this.stratum.on('pool.started', () => {});
    _this.stratum.on('pool.log', (severity, text, separator) => {
      _this.logger[severity]('Pool', 'Checks', [text], separator);
    });

    // Forward network/template events to master so /stats can expose currentHeight.
    // The Pool instance only lives in worker forks — the master's statsAggregator
    // has no direct view, so we ferry the data via IPC.
    _this.stratum.on('pool.network', (networkData) => {
      _this.sendStatsEvent({
        type: 'network',
        chain: networkData.networkType,             // 'primary' | 'auxiliary'
        height: networkData.height,
        difficulty: networkData.difficulty,
        hashrate: networkData.hashrate,
      });
    });

    // Coin symbols (used to tag every stats event so consumers don't have to
    // map 'primary' → LTC / 'auxiliary' → DOGE themselves).
    const primarySymbol = (_this.config.primary && _this.config.primary.coin && _this.config.primary.coin.symbol) || 'PRI';
    const auxSymbol = (_this.config.auxiliary && _this.config.auxiliary.coin && _this.config.auxiliary.coin.symbol) || 'AUX';

    // Handle Stratum Share Events
    //
    // pool.share fires twice for a submission that clears both targets on a
    // merged-mining instance: once with blockType ∈ {'share','primary'} for
    // the parent chain (LTC), and a second time with blockType='auxiliary'
    // when the aux chain (DOGE) target is also hit. Both emissions carry the
    // same shareData fields apart from blockType/height/hash/etc, so we must
    // count *shares* only on the primary emission to avoid double-counting.
    _this.stratum.on('pool.share', (shareData, shareValid, accepted) => {

      if (!shareData) return;
      const address = (shareData.addrPrimary || 'unknown').split('.')[0];
      const worker = (shareData.addrPrimary || '').split('.')[1] || null;

      const isAuxEvent = shareData.blockType === 'auxiliary';
      const chain = isAuxEvent ? 'auxiliary' : 'primary';
      const coin = isAuxEvent ? auxSymbol : primarySymbol;

      // Extract computor index from first 4 bytes of extraNonce2 (big endian) % 676.
      // Only present on the primary emission — the auxiliary outputData strips it.
      let computorIdx = null;
      if (shareData.extraNonce2 && shareData.extraNonce2.length >= 8) {
        const compId = parseInt(shareData.extraNonce2.substring(0, 8), 16);
        if (Number.isFinite(compId)) computorIdx = compId % 676;
      }

      // Processed Share was Accepted
      if (shareValid) {
        // Only log "share accepted" once per submission (on the primary emission).
        if (!isAuxEvent) {
          const text = _this.text.stratumSharesText1(shareData.difficulty, shareData.shareDiff, address, shareData.ip);
          _this.logger['log']('Pool', 'Checks', [text]);
        }

        const isPrimaryBlock = shareData.blockType === 'primary';
        const isAuxiliaryBlock = shareData.blockType === 'auxiliary';

        _this.sendStatsEvent({
          type: 'share',
          valid: true,
          chain: chain,                    // 'primary' | 'auxiliary'
          coin: coin,                      // 'LTC' | 'DOGE' | …
          address: address,
          addrAuxiliary: shareData.addrAuxiliary || null,
          worker: worker,
          isBlock: isPrimaryBlock || isAuxiliaryBlock,
          isPrimaryBlock: isPrimaryBlock,
          isAuxiliaryBlock: isAuxiliaryBlock,
          accepted: !!accepted,
          height: shareData.height,
          hash: shareData.hash,
          difficulty: shareData.difficulty,
          shareDiff: shareData.shareDiff,
          computorIdx: computorIdx,
        });

        if (isPrimaryBlock) {
          _this.logger['special']('Pool', 'Blocks', [`*** ${coin} BLOCK FOUND at height ${shareData.height} by ${address} | hash: ${shareData.hash} | ${accepted ? 'CONFIRMED' : 'pending'} ***`]);
        } else if (isAuxiliaryBlock) {
          _this.logger['special']('Pool', 'Blocks', [`*** ${coin} (aux) BLOCK FOUND at height ${shareData.height} by ${address} | hash: ${shareData.hash} | ${accepted ? 'CONFIRMED' : 'pending'} ***`]);
        }

      // Processed Share was Rejected — invalid shares never trigger an aux emission,
      // so this branch always represents a primary-chain rejection.
      } else {
        const text = _this.text.stratumSharesText2(shareData.error, address, shareData.ip);
        _this.logger['error']('Pool', 'Checks', [text]);

        _this.sendStatsEvent({
          type: 'share',
          valid: false,
          chain: 'primary',
          coin: primarySymbol,
          address: address,
          error: shareData.error,
          computorIdx: computorIdx,
        });
      }
    });
  };

  // Output Stratum Data on Startup
  this.outputStratum = function() {

    // Build Connected Coins
    const coins = [_this.config.primary.coin.name];
    if (_this.config.auxiliary && _this.config.auxiliary.enabled) {
      coins.push(_this.config.auxiliary.coin.name);
    }

    // Build Pool Starting Message
    const output = [
      _this.text.startingMessageText1(`Pool-${ _this.config.primary.coin.name }`),
      _this.text.startingMessageText2(`[${ coins.join(', ') }]`),
      _this.text.startingMessageText3(_this.config.settings.testnet ? 'Testnet' : 'Mainnet'),
      _this.text.startingMessageText4(_this.stratum.statistics.ports.join(', ')),
      _this.text.startingMessageText5(_this.stratum.statistics.feePercentage * 100),
      _this.text.startingMessageText6(_this.stratum.manager.currentJob.rpcData.height),
      _this.text.startingMessageText7(_this.stratum.statistics.difficulty),
      _this.text.startingMessageText8(_this.stratum.statistics.connections),
      _this.text.startingMessageText9()];

    // Send Starting Message to Logger
    if (_this.forkId === '0') {
      _this.logger['log']('Pool', 'Output', output, true);
    }
  };

  // Calculate shares per minute from the last 5 minutes
  this.getSharesPerMinute = function() {
    const now = Date.now();
    const windowMs = 5 * 60 * 1000;
    _this.shareTimestamps = _this.shareTimestamps.filter(t => now - t < windowMs);
    if (_this.shareTimestamps.length === 0) return 0;
    const elapsed = Math.min(windowMs, now - _this.shareTimestamps[0]);
    return elapsed > 0 ? (_this.shareTimestamps.length / (elapsed / 60000)).toFixed(2) : 0;
  };

  // Start HTTP Stats Server (master only)
  this.setupStatsServer = function() {
    const port = configMain.statsPort || 8080;
    const server = http.createServer((req, res) => {
      if (req.url === '/stats' || req.url === '/') {
        const uptime = Math.floor((Date.now() - _this.stats.startTime) / 1000);
        const priCoin = (_this.config.primary && _this.config.primary.coin && _this.config.primary.coin.symbol) || 'PRI';
        const auxEnabled = !!(_this.config.auxiliary && _this.config.auxiliary.enabled);
        const auxCoin = auxEnabled ? ((_this.config.auxiliary.coin && _this.config.auxiliary.coin.symbol) || 'AUX') : null;
        const data = {
          uptime: uptime,
          chains: auxEnabled ? { primary: priCoin, auxiliary: auxCoin } : { primary: priCoin },
          currentHeight: _this.stats.currentHeight || null,
          currentHeightAuxiliary: auxEnabled ? (_this.stats.currentHeightAuxiliary || null) : null,
          network: {
            primary:   { coin: priCoin, difficulty: _this.stats.networkDifficulty || null, hashrate: _this.stats.networkHashrate || null },
            auxiliary: auxEnabled
              ? { coin: auxCoin, difficulty: _this.stats.networkDifficultyAuxiliary || null, hashrate: _this.stats.networkHashrateAuxiliary || null }
              : null,
          },
          shares: { valid: _this.stats.sharesValid, invalid: _this.stats.sharesInvalid, perMinute: parseFloat(_this.getSharesPerMinute()) },
          blocks: {
            primary: { coin: priCoin, found: _this.stats.blocksFound, confirmed: _this.stats.blocksConfirmed },
            auxiliary: auxEnabled
              ? { coin: auxCoin, found: _this.stats.blocksFoundAuxiliary || 0, confirmed: _this.stats.blocksConfirmedAuxiliary || 0 }
              : null,
            // legacy aliases for existing consumers
            found: _this.stats.blocksFound,
            confirmed: _this.stats.blocksConfirmed,
          },
          lastShare: _this.stats.lastShareTime ? new Date(_this.stats.lastShareTime).toISOString() : null,
          lastBlock: _this.stats.lastBlockTime
            ? { coin: priCoin, time: new Date(_this.stats.lastBlockTime).toISOString(), height: _this.stats.lastBlockHeight }
            : null,
          lastBlockAuxiliary: auxEnabled && _this.stats.lastBlockAuxiliaryTime
            ? { coin: auxCoin, time: new Date(_this.stats.lastBlockAuxiliaryTime).toISOString(), height: _this.stats.lastBlockAuxiliaryHeight }
            : null,
          recentBlocks: _this.stats.recentBlocks || [],
          workers: _this.stats.workers,
        };
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(data, null, 2));
      } else {
        res.writeHead(404);
        res.end();
      }
    });
    server.listen(port, () => {
      _this.logger['log']('Pool', 'Stats', [`Stats server running on http://localhost:${port}/stats`]);
    });
  };

  // Periodic Stats Logging + Save (master only)
  this.setupStatsLogging = function() {
    setInterval(() => {
      const uptime = Math.floor((Date.now() - _this.stats.startTime) / 1000);
      const h = Math.floor(uptime / 3600);
      const m = Math.floor((uptime % 3600) / 60);
      const priCoin = (_this.config.primary && _this.config.primary.coin && _this.config.primary.coin.symbol) || 'PRI';
      const auxCoin = (_this.config.auxiliary && _this.config.auxiliary.coin && _this.config.auxiliary.coin.symbol) || 'AUX';
      const auxEnabled = !!(_this.config.auxiliary && _this.config.auxiliary.enabled);
      const blockSummary = auxEnabled
        ? `blocks: ${priCoin}=${_this.stats.blocksFound} (conf=${_this.stats.blocksConfirmed}), ${auxCoin}=${_this.stats.blocksFoundAuxiliary || 0} (conf=${_this.stats.blocksConfirmedAuxiliary || 0})`
        : `blocks found: ${_this.stats.blocksFound} (confirmed: ${_this.stats.blocksConfirmed})`;
      _this.logger['log']('Pool', 'Stats', [
        `uptime: ${h}h${m}m | shares: ${_this.stats.sharesValid} accepted, ${_this.stats.sharesInvalid} rejected (${_this.getSharesPerMinute()}/min) | ${blockSummary} | workers: ${Object.keys(_this.stats.workers).length}`
      ]);
      _this.saveStats();
    }, 60000);
  };

  // Mask an address for public live feed (first 6 + last 4 chars).
  this.maskAddress = function(addr) {
    if (!addr || addr.length < 12) return addr || '';
    return addr.substring(0, 6) + '...' + addr.substring(addr.length - 4);
  };

  // Handle stats event from a worker fork (called on master).
  // Updates local stats AND publishes to Redis live feed from a single connection.
  //
  // Two emissions per merged-mining submission are possible (one primary, one
  // auxiliary). Counters that represent "shares" are incremented only on the
  // primary emission. Block counters are split by chain so callers can see
  // LTC vs DOGE block totals independently.
  const ensureWorker = function(addr) {
    if (!_this.stats.workers[addr]) {
      _this.stats.workers[addr] = { valid: 0, invalid: 0, blocks: 0, blocksAuxiliary: 0 };
    } else if (typeof _this.stats.workers[addr].blocksAuxiliary !== 'number') {
      _this.stats.workers[addr].blocksAuxiliary = 0;
    }
    return _this.stats.workers[addr];
  };

  this.handleStatsEvent = function(event) {
    if (event.type === 'network') {
      if (event.chain === 'primary') {
        _this.stats.currentHeight = event.height;
        _this.stats.networkDifficulty = event.difficulty;
        _this.stats.networkHashrate = event.hashrate;
      } else if (event.chain === 'auxiliary') {
        _this.stats.currentHeightAuxiliary = event.height;
        _this.stats.networkDifficultyAuxiliary = event.difficulty;
        _this.stats.networkHashrateAuxiliary = event.hashrate;
      }
      return;
    }
    if (event.type === 'share') {
      _this.stats.lastShareTime = Date.now();
      if (event.valid) {
        // Share counters: primary emissions only (aux emission is the same submission).
        if (event.chain === 'primary') {
          _this.stats.sharesValid++;
          _this.shareTimestamps.push(Date.now());
          ensureWorker(event.address).valid++;
        } else {
          ensureWorker(event.address); // make sure entry exists for aux-only block credit
        }

        // Live publish for every emission so consumers see both LTC and DOGE shares.
        _this.publishLiveEvent({
          type: 'share',
          ts: Date.now(),
          valid: true,
          chain: event.chain,
          coin: event.coin,
          address: _this.maskAddress(event.address),
          worker: event.worker,
          computorIdx: event.computorIdx,
          difficulty: event.difficulty,
          shareDiff: event.shareDiff,
          isBlock: !!event.isBlock,
          isPrimaryBlock: !!event.isPrimaryBlock,
          isAuxiliaryBlock: !!event.isAuxiliaryBlock,
          height: event.height,
        });

        // Primary-chain block (LTC on merged, DOGE on solo)
        if (event.isPrimaryBlock) {
          _this.stats.blocksFound++;
          if (event.accepted) _this.stats.blocksConfirmed++;
          _this.stats.lastBlockTime = Date.now();
          _this.stats.lastBlockHeight = event.height;
          ensureWorker(event.address).blocks++;
          _this.stats.recentBlocks.unshift({
            chain: 'primary',
            coin: event.coin,
            height: event.height,
            hash: event.hash,
            worker: event.address,
            payout: event.address,
            time: new Date().toISOString(),
            confirmed: !!event.accepted,
          });
          if (_this.stats.recentBlocks.length > 10) _this.stats.recentBlocks.length = 10;
          _this.saveStats();

          _this.publishLiveEvent({
            type: 'block',
            ts: Date.now(),
            chain: 'primary',
            coin: event.coin,
            height: event.height,
            hash: event.hash,
            worker: _this.maskAddress(event.address),
            confirmed: !!event.accepted,
          });
        }

        // Auxiliary-chain block (DOGE on the merged-mining instance)
        if (event.isAuxiliaryBlock) {
          _this.stats.blocksFoundAuxiliary = (_this.stats.blocksFoundAuxiliary || 0) + 1;
          if (event.accepted) _this.stats.blocksConfirmedAuxiliary = (_this.stats.blocksConfirmedAuxiliary || 0) + 1;
          _this.stats.lastBlockAuxiliaryTime = Date.now();
          _this.stats.lastBlockAuxiliaryHeight = event.height;
          ensureWorker(event.address).blocksAuxiliary++;
          _this.stats.recentBlocks.unshift({
            chain: 'auxiliary',
            coin: event.coin,
            height: event.height,
            hash: event.hash,
            worker: event.address,
            payout: event.addrAuxiliary || null,
            time: new Date().toISOString(),
            confirmed: !!event.accepted,
          });
          if (_this.stats.recentBlocks.length > 10) _this.stats.recentBlocks.length = 10;
          _this.saveStats();

          _this.publishLiveEvent({
            type: 'block',
            ts: Date.now(),
            chain: 'auxiliary',
            coin: event.coin,
            height: event.height,
            hash: event.hash,
            worker: _this.maskAddress(event.address),
            payout: event.addrAuxiliary ? _this.maskAddress(event.addrAuxiliary) : null,
            confirmed: !!event.accepted,
          });
        }
      } else {
        _this.stats.sharesInvalid++;
        ensureWorker(event.address).invalid++;

        _this.publishLiveEvent({
          type: 'share',
          ts: Date.now(),
          valid: false,
          chain: event.chain,
          coin: event.coin,
          address: _this.maskAddress(event.address),
          computorIdx: event.computorIdx,
          error: event.error,
        });
      }
    }
  };

  // Setup Pool Stratum Capabilities
  /* eslint-disable */
  this.setupStratum = function(callback) {

    // Build Daemon/Stratum Functionality
    _this.handleStratum(callback);
    _this.stratum.setupPrimaryDaemons(() => {
    _this.stratum.setupAuxiliaryDaemons(() => {
    _this.stratum.setupPorts();
    _this.stratum.setupSettings(() => {
    _this.stratum.setupRecipients();
    _this.stratum.setupManager();
    _this.stratum.setupPrimaryBlockchain(() => {
    _this.stratum.setupAuxiliaryBlockchain(() => {
    _this.stratum.setupFirstJob(() => {
    _this.stratum.setupBlockPolling(() => {
    _this.stratum.setupNetwork(() => {
      _this.outputStratum()
      callback(null)
    })

    // Too Much Indentation
    })})})})})})});
  }
};

module.exports = Stratum;
