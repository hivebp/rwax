using namespace eosio;

// Contract Names
static constexpr name ATOMICASSETS_CONTRACT     = name("atomicassets");
static constexpr name CORE_TOKEN_CONTRACT       = name("eosio.token");
static constexpr name RWAX_TOKEN_CONTRACT       = name("token.rwax");

// Symbols
static constexpr symbol CORE_SYMBOL = symbol("WAX", 8);

// Assets
static const asset ZERO_CORE = asset(0, CORE_SYMBOL);