const { nameToBigInt, TimePoint, expectToThrow } = require("@eosnetwork/vert");
const { Asset, Int64, Name, UInt64, UInt128, TimePointSec } = require('@wharfkit/antelope');
const { assert } = require("chai");
const { blockchain, contracts, getAssets, getCollections, init, initial_state, incrementTime } = require("./setup.spec.ts")
const { ERR_ACCOUNT_NOT_AUTHORIZED, ERR_COLLECTION_DOESNT_EXIST } = require("./helpers.ts");

/* Runs before each test */
beforeEach(async () => {
    blockchain.resetTables()
    await init()
})

/* Runs after each test */
afterEach(async () => {

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

    it('ERR_COLLECTION_DOESNT_EXIST', async () => {
        const action = contracts.rwax.actions.tokenize(['mike', 'collection12', `100.0000 SHIT`, [], [], 'Shit Token', 'QMabcSmall', 'QMabcLarge']).send('mike@active');
        await expectToThrow( action, ERR_COLLECTION_DOESNT_EXIST );
    });  

    it('ERR_ACCOUNT_NOT_AUTHORIZED', async () => {
        const action = contracts.rwax.actions.tokenize(['alice', 'testcollec12', `100.0000 SHIT`, [], [], 'Shit Token', 'QMabcSmall', 'QMabcLarge']).send('alice@active');
        await expectToThrow( action, ERR_ACCOUNT_NOT_AUTHORIZED );
    });        
   
    it('error: supply must be positive', async () => {
        const action = contracts.rwax.actions.tokenize(['mike', 'testcollec12', `0.0000 SHIT`, [], [], 'Shit Token', 'QMabcSmall', 'QMabcLarge']).send('mike@active');
        await expectToThrow( action, `eosio_assert: Must provide positive supply` );
    });    

    it('no templates or factors?', async () => {
        await contracts.rwax.actions.tokenize(['mike', 'testcollec12', `1.0000 SHIT`, [], [], 'Shit Token', 'QMabcSmall', 'QMabcLarge']).send('mike@active');
    });                    
});
