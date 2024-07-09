struct TOKEN {
    name   token_contract;
    symbol token_symbol;
};

struct POOL {
    name    pool;
    symbol  token;
};

struct TEMPLATE {
    int32_t     template_id;
    uint32_t    max_assets_to_tokonize;
};

struct VALUEFACTOR {
    string  value;
    float   factor;
};

struct TRAITFACTOR {
    string              trait_name;
    float               min_value;
    float               max_value;
    float               min_factor;
    float               max_factor;
    vector<VALUEFACTOR> values;
};