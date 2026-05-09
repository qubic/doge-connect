/*
 *
 * Example (Main)
 *
 */

// Main Configuration
////////////////////////////////////////////////////////////////////////////////

// Miscellaneous Configuration
const config = {};
config.language = 'english';
config.identifier = '';

// Logger Configuration
config.logger = {};
config.logger.logColors = true;
config.logger.logLevel = 'log';

// Clustering Configuration
config.clustering = {};
config.clustering.enabled = true;
config.clustering.forks = 'auto';

// TLS Configuration
config.tls = {};
config.tls.ca = '';
config.tls.key = '';
config.tls.cert = '';

// Stats persistence + HTTP /stats endpoint
// Distinct paths/ports from the DOGE-solo instance so both can run side-by-side.
config.statsFile = '/var/foundation/ltc-merged-stats.json';
config.statsPort = 8081; // DOGE-solo uses 8080

// Redis live feed (optional — leave url empty to disable)
config.redis = {};
config.redis.url = '';
config.redis.channel = 'ltc-doge:shares';

// Export Configuration
module.exports = config;
