const { assert } = require("chai");

// Compares 2 values and makes sure they are within the specified threshold of each other
function almost_equal(actual, expected, tolerance = 0.000003) {
    const difference = Math.abs(actual - expected);
    const relativeError = difference / Math.abs(expected);
    assert.isTrue(relativeError <= tolerance, `Expected ${actual} to be within ${tolerance * 100}% of ${expected}`);
}

// Error Messages
const ERR_LISTING_DOESNT_EXIST = "eosio_assert: listing ID not found";

// Enums
const STATUSES = Object.freeze({ AWAITING_DEPOSIT: 0, DEPOSIT_MADE: 1, BORROWED: 2, LIQUIDATED: 3 });

const wax = (amount) => {
    //console.log(`${parseFloat(amount).toFixed(8)} WAX`);
    return `${parseFloat(amount).toFixed(8)} WAX`
}

module.exports = {
    almost_equal,
    ERR_LISTING_DOESNT_EXIST,
    STATUSES,
    wax
}