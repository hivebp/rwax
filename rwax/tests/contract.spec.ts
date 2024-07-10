const { nameToBigInt, TimePoint, expectToThrow } = require("@eosnetwork/vert");
const { Asset, Int64, Name, UInt64, UInt128, TimePointSec } = require('@wharfkit/antelope');
const { assert } = require("chai");
const { blockchain, contracts, getAssets, init, initial_state, incrementTime } = require("./setup.spec.ts")

/* Runs before each test */
beforeEach(async () => {
    blockchain.resetTables()
    await init()
})

/* Runs after each test */
afterEach(async () => {
    // make sure global counter matches length of listings
})

/**
 * Contracts Needed:
 * 
 * ✔️ Atomicassets
 * ✔️ eosio.token
 * 
 */

describe('tokenize action', () => {

    it('error: missing auth', async () => {
        const action = contracts.rwax.actions.tokenize(['mike', 'collection12', `100.0000 SHIT`, [], [], 'Shit Token', 'QMabcSmall', 'QMabcLarge']).send('eosio@active');
        await expectToThrow( action, `missing required authority mike` );
    });  
                         
});
