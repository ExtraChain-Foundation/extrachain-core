from extrachain import action, contract, init, query, require


@contract
class Counter:
    def __init__(self, state):
        self.state = state

    @init
    def init(self, context, initial=0):
        require(initial >= 0, "Counter cannot be negative")
        self.state["owner"] = context.caller
        self.state["value"] = initial
        return initial

    @action
    def increment(self, context, amount=1):
        require(amount > 0, "Increment must be positive")
        self.state["value"] += amount
        context.emit("incremented", self.state["value"])
        return self.state["value"]

    @query
    def value(self, context):
        return self.state["value"]
