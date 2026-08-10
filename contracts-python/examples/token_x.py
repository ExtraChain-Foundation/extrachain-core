from extrachain import Amount, action, authorize_upgrade, contract, init, migrate, owner_only, query, require


@contract
class TokenX:
    def __init__(self, state):
        self.state = state

    def balance(self, actor):
        return self.state["balances"].get(actor, 0)

    def locked(self, actor):
        return self.state["locked"].get(actor, 0)

    @init
    def init(self, context, name, symbol, decimals, supply, initial_balances):
        require(0 <= decimals <= 18, "Token decimals are out of range")
        supply = Amount(supply).value
        self.state.update({
            "name": name,
            "symbol": symbol,
            "decimals": decimals,
            "owner": context.caller,
            "total_supply": supply,
            "freeze_last_enabled": False,
            "balances": {},
            "locked": {},
        })
        if initial_balances:
            total = 0
            for actor, amount in initial_balances:
                amount = Amount(amount).value
                require(actor not in self.state["balances"] and amount > 0, "Migration balance is invalid")
                self.state["balances"][actor] = amount
                total = Amount(total).checked_add(Amount(amount)).value
            require(total == supply, "Migration supply does not match balances")
        else:
            self.state["balances"][context.caller] = supply
        return supply

    @migrate
    def migrate(self, context):
        self.state["freeze_last_enabled"] = True
        return None

    @authorize_upgrade
    @owner_only
    def authorize_upgrade(self, context, new_hash):
        require(len(new_hash) > 0, "Upgrade hash is missing")
        return True

    @action
    def transfer(self, context, receiver, amount):
        amount = Amount(amount).value
        require(receiver != context.caller and amount > 0, "Transfer is invalid")
        available = self.balance(context.caller) - self.locked(context.caller)
        require(available >= amount, "Balance is too small")
        self.state["balances"][context.caller] = self.balance(context.caller) - amount
        self.state["balances"][receiver] = Amount(self.balance(receiver)).checked_add(Amount(amount)).value
        unit = 10 ** self.state["decimals"]
        if self.state["freeze_last_enabled"] and self.balance(context.caller) == unit:
            self.state["locked"][context.caller] = unit
            context.emit("lock", [[context.caller, unit]])
        context.emit("transfer", [[context.caller, amount], [receiver, amount]])
        return None

    @query
    def balance_of(self, context, actor):
        return self.balance(actor)

    @query
    def locked_balance_of(self, context, actor):
        return self.locked(actor)
