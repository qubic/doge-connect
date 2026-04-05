const crypto = require('crypto');

////////////////////////////////////////////////////////////////////////////////

// Main Algorithms Function
const Algorithms = {

  // Scrypt Algorithm
  'scrypt': {
    multiplier: Math.pow(2, 16),
    diff: parseInt('0x00000000ffff0000000000000000000000000000000000000000000000000000'),
    hash: function() {
      return function(data) {
        return crypto.scryptSync(data, data, 32, { N: 1024, r: 1, p: 1 });
      };
    }
  },

  // Sha256d Algorithm
  'sha256d': {
    multiplier: 1,
    diff: parseInt('0x00000000ffff0000000000000000000000000000000000000000000000000000'),
    hash: function() {
      return function(data) {
        const h1 = crypto.createHash('sha256').update(data).digest();
        return crypto.createHash('sha256').update(h1).digest();
      };
    }
  },
};

module.exports = Algorithms;
