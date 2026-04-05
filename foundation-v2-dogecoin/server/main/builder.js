const Stratum = require('./stratum');
const Text = require('../../locales/index');
const cluster = require('cluster');
const utils = require('./utils');

////////////////////////////////////////////////////////////////////////////////

// Main Builder Function
const Builder = function(logger, configMain) {

  const _this = this;
  this.logger = logger;
  this.configMain = configMain;
  this.text = Text[configMain.language];

  // Builder Variables
  this.workers = {};
  this.numWorkers = 0;

  // Stats aggregator (runs on master only)
  this.statsAggregator = null;

  // Handle Pool Worker Creation
  /* istanbul ignore next */
  this.createPoolWorkers = function(forkId) {

    // Build Worker from Data
    _this.workers[forkId] = cluster.fork({
      config: JSON.stringify(_this.config),
      configMain: JSON.stringify(_this.configMain),
      forkId: forkId,
      type: 'worker',
    });

    // Listen for stats events from worker
    _this.workers[forkId].on('message', (msg) => {
      if (msg && msg.type === 'stats' && _this.statsAggregator) {
        _this.statsAggregator.handleStatsEvent(msg.event);
      }
    });

    // Handle Worker Failover
    _this.workers[forkId].on('exit', () => {
      const lines = [_this.text.builderWorkersText1(forkId)];
      _this.logger.error('Builder', 'Workers', lines);
      setTimeout(() => {
        _this.createPoolWorkers(forkId);
      }, 2000);
    });
  };

  // Functionality for Pool Workers
  /* istanbul ignore next */
  this.setupPoolWorkers = function() {

    // Count Number of Process Forks
    const numForks = utils.countProcessForks(_this.configMain);

    // Check if any Valid Configs Exist
    if (numForks === 0) {
      const lines = [_this.text.builderWorkersText2()];
      _this.logger.error('Builder', 'Workers', lines);
      return;
    }

    // Setup stats aggregator on master
    _this.statsAggregator = new Stratum(_this.logger, _this.config, _this.configMain);
    _this.statsAggregator.setupStatsServer();
    _this.statsAggregator.setupStatsLogging();

    // Create Pool Workers
    const startInterval = setInterval(() => {
      _this.createPoolWorkers(_this.numWorkers);
      _this.numWorkers += 1;
      if (_this.numWorkers === numForks) {
        const lines = [_this.text.builderWorkersText2(1, numForks)];
        _this.logger.debug('Builder', 'Workers', lines);
        clearInterval(startInterval);
      }
    }, 250);
  };
};

module.exports = Builder;
