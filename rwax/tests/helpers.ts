const { assert } = require("chai");

// Compares 2 values and makes sure they are within the specified threshold of each other
function almost_equal(actual, expected, tolerance = 0.000003) {
    const difference = Math.abs(actual - expected);
    const relativeError = difference / Math.abs(expected);
    assert.isTrue(relativeError <= tolerance, `Expected ${actual} to be within ${tolerance * 100}% of ${expected}`);
}

// Error Messages
const ERR_ACCOUNT_NOT_AUTHORIZED = "eosio_assert: Account is not authorized";
const ERR_COLLECTION_DOESNT_EXIST = "eosio_assert: No collection with this name exists";

const wax = (amount) => {
    //console.log(`${parseFloat(amount).toFixed(8)} WAX`);
    return `${parseFloat(amount).toFixed(8)} WAX`
}

module.exports = {
    almost_equal,
    ERR_ACCOUNT_NOT_AUTHORIZED,
    ERR_COLLECTION_DOESNT_EXIST,
    wax
}