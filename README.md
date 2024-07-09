# RWAX

How does this contract work?

1) tokenize:
- must be collection owner
- set the maximum supply of your token
- add a list of templates to be included and their max tokenizable supply
- define trait factors that affect the value of a single NFT
- Max supply is split among templates and they're emplaced in a table to access them quickly
- Trait factors are evaluated and the max factor is ascertained. Trait factors are saved for the created token
- token.rwax is called, token is created and issued

2) receive_asset_transfer:
- emplace assets in temporary transfers

3) tokenizenfts
- take assets from transfer
- tokenize each asset
- determine template of asset
- find token for template
- calculate asset value based on trait factors
- check if asset should be sent to a pool (separate account to farm rewards)
- send out tokens

4) receive_transfer
- receive token, check if memo is redeem, add it to user balances

5) redeem
- get amount from balances
- find asset
- determine value based on traits
- check if balance is enough
- check if asset is in pool, request asset back
- send asset to user