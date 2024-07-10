const { Blockchain, nameToBigInt, TimePoint, expectToThrow } = require("@eosnetwork/vert");
const { Asset, Int64, Name, UInt64, UInt128, TimePointSec } = require('@wharfkit/antelope');
const { assert } = require("chai");
const { wax } = require("./helpers.ts")
const blockchain = new Blockchain();

const [a, b, c, d, e] = blockchain.createAccounts('mike', 'hive', 'rwaxtester', 'eosio', 'alice')

const contracts = {
    atomicassets: blockchain.createContract('atomicassets', '../atomicassets/build/atomicassets'),
    rwax: blockchain.createContract('rwax', 'build/rwax'),
    wax: blockchain.createContract('eosio.token', '../eosio.token/build/token')
}

const scopes = {
    atomicassets: contracts.atomicassets.value,
    rwax: contracts.rwax.value
}

const initial_state = {
    chain_time: 1710460800,
    wax_supply: `46116860184.27387903 WAX`,
}

const setTime = async (seconds) => {
    const time = TimePointSec.fromInteger(seconds);
    return blockchain.setTime(time);    
}

function incrementTime(seconds = TEN_MINUTES) {
    const time = TimePointSec.fromInteger(seconds);
    return blockchain.addTime(time);
}

const getAtomicConfig = async (log = false) => {
    const g = await contracts.atomicassets.tables
        .config(scopes.atomicassets)
        .getTableRows()[0]
    if(log){
        console.log('atomic config:')
        console.log(g)  
    }
    return g  
}

const getAssets = async (user, log = false) => {
    const scope = Name.from(user).value.value
    const assets = await contracts.atomicassets.tables
        .assets(scope)
        .getTableRows()
    if(log){
        console.log('assets:')
        console.log(assets)  
    }
    return assets 
}

const getCollections = async (log = false) => {

    const collections = await contracts.atomicassets.tables
        .collections(scopes.atomicassets)
        .getTableRows()
    if(log){
        console.log('collections:')
        console.log(collections)  
    }
    return collections 
}

const collection_data = [
    { first: "name", second: ["string", "Test Col"] },
    { first: "description", second: ["string", "something blah"] },
    { first: "url", second: ["string", "https://hi.wtf"] },
    { first: "socials", second: ["string", JSON.stringify({ twitter: "", medium: "", facebook: "", github: "", discord: "", youtube: "", telegram: "" })] },
    { first: "creator_info", second: ["string", JSON.stringify({ country: "", address: "", city: "", zip: "", company: "", name: "", registration_number: "" })] },
    { first: "images", second: ["string", JSON.stringify({ banner_1920x500: "QmNkeGgWH9hBPNms5yPXBSwvsPeBLZsrnAHbqrSDGk2X35", logo_512x512: "QmcioWDWaSXx2MWN6vR7dbKWnDvHqvdmSzSXfaBfpxKNfK" })] }
];


const col_format = [
  {"name": "name", "type": "string"},
  {"name": "description", "type": "string"},
  {"name": "url", "type": "string"},
  {"name": "socials", "type": "string"},
  {"name": "creator_info", "type": "string"},
  {"name": "images", "type": "string"}
]

const schema_format = [
  {"name": "name", "type": "string"},
  {"name": "img", "type": "string"},
  {"name": "rarity", "type": "uint8"}
]

const init = async () => {
    await setTime(initial_state.chain_time);
    await contracts.wax.actions.create(['eosio', initial_state.wax_supply]).send();
    await contracts.wax.actions.issue(['eosio', initial_state.wax_supply, 'issuing wax']).send('eosio@active');
    await contracts.wax.actions.transfer(['eosio', 'mike', wax(100000), 'sending wax to mike']).send('eosio@active');
    await contracts.wax.actions.transfer(['eosio', 'hive', wax(100000), 'sending wax to hive']).send('eosio@active');
    await contracts.wax.actions.transfer(['eosio', 'rwaxtester', wax(100000), 'sending wax to rwaxtester']).send('eosio@active');
    await contracts.wax.actions.transfer(['eosio', 'alice', wax(100000), 'sending wax to alice']).send('eosio@active');
    await contracts.atomicassets.actions.init([]).send('atomicassets@active');
    await contracts.rwax.actions.init([]).send('rwax@active');
    await contracts.atomicassets.actions.admincoledit([col_format]).send('atomicassets@active')
    await contracts.atomicassets.actions.createcol(['mike', 'testcollec12', true, ['mike'], [], 0.1, collection_data]).send('mike@active');
    await contracts.atomicassets.actions.createschema(['mike', 'testcollec12', 'testschema', schema_format]).send('mike@active');
    await contracts.atomicassets.actions.mintasset(['mike', 'testcollec12', 'testschema', -1, 'mike', [], [], []]).send('mike@active');
    await contracts.atomicassets.actions.mintasset(['mike', 'testcollec12', 'testschema', -1, 'mike', [], [], []]).send('mike@active');
}

module.exports = {
    a, b, c, d, e,
    blockchain,
    contracts,
    getAssets,
    getCollections,
    incrementTime,
    init,
    initial_state,
    setTime,
}