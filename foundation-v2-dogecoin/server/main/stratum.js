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
      blocksFound: 0,
      blocksConfirmed: 0,
      lastShareTime: null,
      lastBlockTime: null,
      lastBlockHeight: null,
      recentBlocks: [],
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

    // Handle Stratum Share Events
    _this.stratum.on('pool.share', (shareData, shareValid, accepted) => {

      if (!shareData) return;
      const address = (shareData.addrPrimary || 'unknown').split('.')[0];
      const worker = (shareData.addrPrimary || '').split('.')[1] || null;

      // Extract computor index from first 4 bytes of extraNonce2 (big endian) % 676.
      // The dispatcher encodes the computor ID into extraNonce2[0..3].
      let computorIdx = null;
      if (shareData.extraNonce2 && shareData.extraNonce2.length >= 8) {
        const compId = parseInt(shareData.extraNonce2.substring(0, 8), 16);
        if (Number.isFinite(compId)) computorIdx = compId % 676;
      }

      // Processed Share was Accepted
      if (shareValid) {
        const text = _this.text.stratumSharesText1(shareData.difficulty, shareData.shareDiff, address, shareData.ip);
        _this.logger['log']('Pool', 'Checks', [text]);

        // Send event to master for aggregation + Redis publish.
        _this.sendStatsEvent({
          type: 'share',
          valid: true,
          address: address,
          worker: worker,
          isBlock: shareData.blockType === 'primary',
          accepted: !!accepted,
          height: shareData.height,
          hash: shareData.hash,
          difficulty: shareData.difficulty,
          shareDiff: shareData.shareDiff,
          computorIdx: computorIdx,
        });

        // Check if share is a block
        if (shareData.blockType === 'primary') {
          _this.logger['special']('Pool', 'Blocks', [`*** BLOCK FOUND at height ${shareData.height} by ${address} | hash: ${shareData.hash} | ${accepted ? 'CONFIRMED' : 'pending'} ***`]);
        }

      // Processed Share was Rejected
      } else {
        const text = _this.text.stratumSharesText2(shareData.error, address, shareData.ip);
        _this.logger['error']('Pool', 'Checks', [text]);

        // Send event to master for aggregation + Redis publish.
        _this.sendStatsEvent({
          type: 'share',
          valid: false,
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
        const data = {
          uptime: uptime,
          currentHeight: _this.stratum && _this.stratum.manager.currentJob ? _this.stratum.manager.currentJob.rpcData.height : null,
          shares: { valid: _this.stats.sharesValid, invalid: _this.stats.sharesInvalid, perMinute: parseFloat(_this.getSharesPerMinute()) },
          blocks: { found: _this.stats.blocksFound, confirmed: _this.stats.blocksConfirmed },
          lastShare: _this.stats.lastShareTime ? new Date(_this.stats.lastShareTime).toISOString() : null,
          lastBlock: _this.stats.lastBlockTime ? { time: new Date(_this.stats.lastBlockTime).toISOString(), height: _this.stats.lastBlockHeight } : null,
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
      _this.logger['log']('Pool', 'Stats', [
        `uptime: ${h}h${m}m | shares: ${_this.stats.sharesValid} accepted, ${_this.stats.sharesInvalid} rejected (${_this.getSharesPerMinute()}/min) | blocks found: ${_this.stats.blocksFound} (confirmed: ${_this.stats.blocksConfirmed}) | workers: ${Object.keys(_this.stats.workers).length}`
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
  this.handleStatsEvent = function(event) {
    if (event.type === 'share') {
      _this.stats.lastShareTime = Date.now();
      if (event.valid) {
        _this.stats.sharesValid++;
        _this.shareTimestamps.push(Date.now());
        if (!_this.stats.workers[event.address]) _this.stats.workers[event.address] = { valid: 0, invalid: 0, blocks: 0 };
        _this.stats.workers[event.address].valid++;

        // Publish share to Redis live feed (mask worker address for privacy).
        _this.publishLiveEvent({
          type: 'share',
          ts: Date.now(),
          valid: true,
          address: _this.maskAddress(event.address),
          worker: event.worker,
          computorIdx: event.computorIdx,
          difficulty: event.difficulty,
          shareDiff: event.shareDiff,
          isBlock: !!event.isBlock,
          height: event.height,
        });

        if (event.isBlock) {
          _this.stats.blocksFound++;
          _this.stats.lastBlockTime = Date.now();
          _this.stats.lastBlockHeight = event.height;
          _this.stats.workers[event.address].blocks++;
          if (event.accepted) _this.stats.blocksConfirmed++;
          _this.stats.recentBlocks.unshift({
            height: event.height,
            hash: event.hash,
            worker: event.address,
            time: new Date().toISOString(),
            confirmed: !!event.accepted,
          });
          if (_this.stats.recentBlocks.length > 10) _this.stats.recentBlocks.length = 10;
          _this.saveStats();

          // Publish block event to Redis live feed.
          _this.publishLiveEvent({
            type: 'block',
            ts: Date.now(),
            height: event.height,
            hash: event.hash,
            worker: _this.maskAddress(event.address),
            confirmed: !!event.accepted,
          });
        }
      } else {
        _this.stats.sharesInvalid++;
        if (!_this.stats.workers[event.address]) _this.stats.workers[event.address] = { valid: 0, invalid: 0, blocks: 0 };
        _this.stats.workers[event.address].invalid++;

        // Publish rejection to Redis live feed.
        _this.publishLiveEvent({
          type: 'share',
          ts: Date.now(),
          valid: false,
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
