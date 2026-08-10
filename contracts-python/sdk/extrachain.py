ABI_VERSION = 4
MAX_PROOFS = 64
MAX_EVENTS = 64
MAX_EFFECTS = 64
MAX_COLLECTION_ENTRIES = 16384
MAX_U128 = 340282366920938463463374607431768211455


class ContractError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ContractError(message)


class Decoder:
    def __init__(self, source):
        self.source = source
        self.offset = 0

    def byte(self):
        if self.offset >= len(self.source):
            raise ContractError("Invalid contract data")
        value = self.source[self.offset]
        self.offset += 1
        return value

    def integer(self, length):
        value = 0
        for _ in range(length):
            value = (value << 8) | self.byte()
        return value

    def length(self, marker, fixed_mask, fixed_value, marker8, marker16, marker32):
        if marker & fixed_mask == fixed_value:
            return marker & ~fixed_mask
        if marker == marker8:
            return self.integer(1)
        if marker == marker16:
            return self.integer(2)
        if marker == marker32:
            return self.integer(4)
        raise ContractError("Invalid contract data")

    def value(self):
        marker = self.byte()
        if marker <= 0x7f:
            return marker
        if marker >= 0xe0:
            return marker - 256
        if marker == 0xc0:
            return None
        if marker == 0xc2:
            return False
        if marker == 0xc3:
            return True
        if marker in (0xcc, 0xcd, 0xce, 0xcf):
            return self.integer((1, 2, 4, 8)[marker - 0xcc])
        if marker in (0xd0, 0xd1, 0xd2, 0xd3):
            length = (1, 2, 4, 8)[marker - 0xd0]
            value = self.integer(length)
            sign = 1 << (length * 8 - 1)
            return value - (sign << 1) if value & sign else value
        if marker & 0xe0 == 0xa0 or marker in (0xd9, 0xda, 0xdb):
            length = self.length(marker, 0xe0, 0xa0, 0xd9, 0xda, 0xdb)
            return self.take(length).decode("utf-8")
        if marker in (0xc4, 0xc5, 0xc6):
            length = self.integer((1, 2, 4)[marker - 0xc4])
            return self.take(length)
        if marker & 0xf0 == 0x90 or marker in (0xdc, 0xdd):
            length = marker & 0x0f if marker & 0xf0 == 0x90 else self.integer(2 if marker == 0xdc else 4)
            result = []
            for _ in range(length):
                result.append(self.value())
            return result
        if marker & 0xf0 == 0x80 or marker in (0xde, 0xdf):
            length = marker & 0x0f if marker & 0xf0 == 0x80 else self.integer(2 if marker == 0xde else 4)
            result = {}
            for _ in range(length):
                key = self.value()
                require(isinstance(key, str), "Contract map keys must be strings")
                result[key] = self.value()
            return result
        raise ContractError("Unsupported MessagePack value")

    def take(self, length):
        end = self.offset + length
        if end > len(self.source):
            raise ContractError("Invalid contract data")
        result = bytearray()
        while self.offset < end:
            result.append(self.source[self.offset])
            self.offset += 1
        return bytes(result)

    def finish(self):
        result = self.value()
        require(self.offset == len(self.source), "Contract data has trailing bytes")
        return result


def _unsigned(value, marker, length):
    result = bytearray((marker,))
    for shift in range(length - 1, -1, -1):
        result.append((value >> (shift * 8)) & 0xff)
    return bytes(result)


def encode(value):
    if value is None:
        return b"\xc0"
    if value is False:
        return b"\xc2"
    if value is True:
        return b"\xc3"
    if isinstance(value, int):
        if 0 <= value <= 0x7f:
            return bytes((value,))
        if -32 <= value < 0:
            return bytes((value + 256,))
        if value >= 0:
            require(value <= 0xffffffffffffffff, "Integer is out of ABI range")
            if value <= 0xff:
                return _unsigned(value, 0xcc, 1)
            if value <= 0xffff:
                return _unsigned(value, 0xcd, 2)
            if value <= 0xffffffff:
                return _unsigned(value, 0xce, 4)
            return _unsigned(value, 0xcf, 8)
        require(value >= -0x8000000000000000, "Integer is out of ABI range")
        return _unsigned(value & 0xffffffffffffffff, 0xd3, 8)
    if isinstance(value, str):
        payload = value.encode("utf-8")
        length = len(payload)
        if length <= 31:
            return bytes((0xa0 | length,)) + payload
        if length <= 0xff:
            return _unsigned(length, 0xd9, 1) + payload
        if length <= 0xffff:
            return _unsigned(length, 0xda, 2) + payload
        return _unsigned(length, 0xdb, 4) + payload
    if isinstance(value, (bytes, bytearray)):
        payload = bytes(value)
        length = len(payload)
        if length <= 0xff:
            return _unsigned(length, 0xc4, 1) + payload
        if length <= 0xffff:
            return _unsigned(length, 0xc5, 2) + payload
        return _unsigned(length, 0xc6, 4) + payload
    if isinstance(value, (list, tuple)):
        length = len(value)
        prefix = bytes((0x90 | length,)) if length <= 15 else _unsigned(length, 0xdc, 2)
        result = prefix
        for item in value:
            result += encode(item)
        return result
    if isinstance(value, dict):
        keys = sorted(value.keys())
        length = len(keys)
        prefix = bytes((0x80 | length,)) if length <= 15 else _unsigned(length, 0xde, 2)
        result = prefix
        for key in keys:
            require(isinstance(key, str), "Contract map keys must be strings")
            result += encode(key)
            result += encode(value[key])
        return result
    raise ContractError("Unsupported contract value")


def decode(source):
    return Decoder(source).finish()


class ActorId:
    def __init__(self, value):
        require(isinstance(value, str) and len(value) > 0, "Actor ID is invalid")
        self.value = value

    def __str__(self):
        return self.value


class Amount:
    def __init__(self, value):
        if isinstance(value, str):
            require(value and (value == "0" or not value.startswith("0")), "Amount is invalid")
            for character in value:
                require("0" <= character <= "9", "Amount is invalid")
            value = int(value)
        require(isinstance(value, int) and 0 <= value <= MAX_U128, "Amount is out of range")
        self.value = value

    def checked_add(self, other):
        result = self.value + other.value
        require(result <= MAX_U128, "Amount overflow")
        return Amount(result)

    def checked_sub(self, other):
        require(self.value >= other.value, "Amount underflow")
        return Amount(self.value - other.value)

    def __str__(self):
        return str(self.value)


class StateMap:
    def __init__(self, value=None):
        self.data = {} if value is None else value

    def get(self, key, default=None):
        return self.data.get(key, default)

    def set(self, key, value):
        require(key in self.data or len(self.data) < MAX_COLLECTION_ENTRIES, "State map is full")
        self.data[key] = value

    def remove(self, key):
        return self.data.pop(key, None)


class StateSet:
    def __init__(self, value=None):
        self.data = [] if value is None else value

    def add(self, value):
        if value not in self.data:
            require(len(self.data) < MAX_COLLECTION_ENTRIES, "State set is full")
            self.data.append(value)
            self.data.sort()

    def remove(self, value):
        if value in self.data:
            self.data.remove(value)

    def contains(self, value):
        return value in self.data


class Context:
    def __init__(self, values, verified):
        self.sender, self.caller, self.contract_id, self.block, self.depth = values
        self.dag_proofs, self.dfs_proofs = verified
        require(len(self.dag_proofs) <= MAX_PROOFS and len(self.dfs_proofs) <= MAX_PROOFS, "Too many proofs")
        self.events = []
        self.effects = []

    def emit(self, topic, value):
        require(len(self.events) < MAX_EVENTS, "Too many contract events")
        self.events.append([topic, encode(value)])

    def contract_call(self, target, operation, arguments):
        require(len(self.effects) < MAX_EFFECTS, "Too many contract effects")
        self.effects.append(["contract_call", target, operation, encode(arguments)])

    def token_delta(self, target, operation, arguments):
        require(len(self.effects) < MAX_EFFECTS, "Too many contract effects")
        self.effects.append(["token_delta", target, operation, encode(arguments)])

    def dfs_write(self, target, operation, arguments):
        require(len(self.effects) < MAX_EFFECTS, "Too many contract effects")
        self.effects.append(["dfs_write", target, operation, encode(arguments)])


_contract_type = None
_routes = {}
_owner_only = []


def contract(contract_type):
    global _contract_type
    _contract_type = contract_type
    return contract_type


def _route(kind):
    def decorate(function):
        require(function.__name__ not in _routes, "Contract route names must be unique")
        _routes[function.__name__] = (kind, function)
        return function
    return decorate


init = _route("init")
action = _route("action")
query = _route("query")
authorize_upgrade = _route("authorize_upgrade")
migrate = _route("migrate")


def owner_only(function):
    _owner_only.append(function)
    return function


def _response(ok, state, data=b"", events=None, effects=None, error=None):
    return encode([ok, state, data, [] if events is None else events, [] if effects is None else effects, error])


def __exc_dispatch(request_bytes):
    original_state = b""
    try:
        request = decode(request_bytes)
        require(isinstance(request, list) and len(request) == 6, "Invalid ABI request")
        context_values, method, arguments_bytes, original_state, verified, abi = request
        require(abi == ABI_VERSION, "Unsupported contract ABI")
        require(_contract_type is not None and method in _routes, "Contract method is not available")
        state = {} if len(original_state) == 0 else decode(original_state)
        require(isinstance(state, dict), "Contract state is invalid")
        arguments = decode(arguments_bytes)
        require(isinstance(arguments, list), "Contract arguments must be an array")
        context = Context(context_values, verified)
        kind, function = _routes[method]
        instance = _contract_type(state)
        if function in _owner_only:
            require(state.get("owner") == context.caller, "Owner authority is required")
        value = function(instance, context, *arguments)
        next_state = original_state if kind == "query" else encode(instance.state)
        return _response(True, next_state, encode(value), context.events, context.effects)
    except Exception as error:
        return _response(False, original_state, error=str(error))
