#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::convert::Infallible;
use core::fmt;

use rmp::decode::{
    RmpRead, read_array_len, read_bin_len, read_bool, read_int, read_nil, read_str_len,
};
use rmp::encode::{
    write_array_len, write_bin, write_bool, write_nil, write_sint, write_str, write_u64,
};

pub use extrachain_contract_macros::{ContractCodec, ContractState, contract};

pub const ABI_VERSION: u32 = 4;
pub const MAX_PROOFS: u32 = 64;
pub const MAX_COLLECTION_ENTRIES: u32 = 16_384;

#[cfg(target_family = "wasm")]
#[global_allocator]
static ALLOCATOR: dlmalloc::GlobalDlmalloc = dlmalloc::GlobalDlmalloc;

#[derive(Debug, PartialEq, Eq)]
pub enum CodecError {
    InvalidData,
    InvalidUtf8,
    LimitExceeded,
}

pub struct Decoder<'a> {
    source: &'a [u8],
}

impl<'a> Decoder<'a> {
    #[must_use]
    pub fn new(source: &'a [u8]) -> Self {
        Self { source }
    }

    pub fn array(&mut self) -> Result<u32, CodecError> {
        read_array_len(&mut self.source).map_err(|_| CodecError::InvalidData)
    }

    pub fn string(&mut self) -> Result<String, CodecError> {
        let length = read_str_len(&mut self.source).map_err(|_| CodecError::InvalidData)? as usize;
        if length > self.source.len() {
            return Err(CodecError::LimitExceeded);
        }

        let mut bytes = alloc::vec![0; length];
        self.source
            .read_exact_buf(&mut bytes)
            .map_err(|_| CodecError::InvalidData)?;
        String::from_utf8(bytes).map_err(|_| CodecError::InvalidUtf8)
    }

    pub fn bytes(&mut self) -> Result<Vec<u8>, CodecError> {
        let length = read_bin_len(&mut self.source).map_err(|_| CodecError::InvalidData)? as usize;
        if length > self.source.len() {
            return Err(CodecError::LimitExceeded);
        }

        let mut bytes = alloc::vec![0; length];
        self.source
            .read_exact_buf(&mut bytes)
            .map_err(|_| CodecError::InvalidData)?;
        Ok(bytes)
    }

    pub fn u64(&mut self) -> Result<u64, CodecError> {
        read_int(&mut self.source).map_err(|_| CodecError::InvalidData)
    }

    pub fn i64(&mut self) -> Result<i64, CodecError> {
        read_int(&mut self.source).map_err(|_| CodecError::InvalidData)
    }

    pub fn amount(&mut self) -> Result<u128, CodecError> {
        let marker = self
            .source
            .first()
            .copied()
            .ok_or(CodecError::InvalidData)?;
        if marker & 0xe0 == 0xa0 || matches!(marker, 0xd9..=0xdb) {
            let value = self.string()?;
            if value.is_empty()
                || (value.len() > 1 && value.starts_with('0'))
                || !value.bytes().all(|byte| byte.is_ascii_digit())
            {
                return Err(CodecError::InvalidData);
            }
            return value.parse::<u128>().map_err(|_| CodecError::InvalidData);
        }
        self.u64().map(u128::from)
    }

    pub fn boolean(&mut self) -> Result<bool, CodecError> {
        read_bool(&mut self.source).map_err(|_| CodecError::InvalidData)
    }

    pub fn nil(&mut self) -> Result<(), CodecError> {
        read_nil(&mut self.source).map_err(|_| CodecError::InvalidData)
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.source.is_empty()
    }
}

#[derive(Default)]
pub struct Encoder {
    output: Vec<u8>,
}

impl Encoder {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn array(&mut self, length: u32) {
        write_array_len(&mut self.output, length).expect("writing to memory cannot fail");
    }

    pub fn string(&mut self, value: &str) {
        write_str(&mut self.output, value).expect("writing to memory cannot fail");
    }

    pub fn bytes(&mut self, value: &[u8]) {
        write_bin(&mut self.output, value).expect("writing to memory cannot fail");
    }

    pub fn u64(&mut self, value: u64) {
        write_u64(&mut self.output, value).expect("writing to memory cannot fail");
    }

    pub fn i64(&mut self, value: i64) {
        write_sint(&mut self.output, value).expect("writing to memory cannot fail");
    }

    pub fn amount(&mut self, value: u128) {
        self.string(&value.to_string());
    }

    pub fn boolean(&mut self, value: bool) {
        write_bool(&mut self.output, value).expect("writing to memory cannot fail");
    }

    pub fn nil(&mut self) {
        let result: Result<(), Infallible> = write_nil(&mut self.output);
        match result {
            Ok(()) => {}
            Err(error) => match error {},
        }
    }

    #[must_use]
    pub fn finish(self) -> Vec<u8> {
        self.output
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ContractError {
    message: &'static str,
}

impl ContractError {
    #[must_use]
    pub const fn new(message: &'static str) -> Self {
        Self { message }
    }

    #[must_use]
    pub fn codec(_: CodecError) -> Self {
        Self::new("Invalid contract data")
    }
}

impl fmt::Display for ContractError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl From<&'static str> for ContractError {
    fn from(message: &'static str) -> Self {
        Self::new(message)
    }
}

pub type ContractResult<T> = Result<T, ContractError>;

pub trait ContractValue: Sized {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self>;
    fn encode_value(&self, encoder: &mut Encoder);
}

impl ContractValue for () {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        decoder.nil().map_err(ContractError::codec)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.nil();
    }
}

impl ContractValue for bool {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        decoder.boolean().map_err(ContractError::codec)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.boolean(*self);
    }
}

impl ContractValue for u8 {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        u8::try_from(decoder.u64().map_err(ContractError::codec)?)
            .map_err(|_| ContractError::new("Unsigned byte is out of range"))
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.u64(u64::from(*self));
    }
}

macro_rules! unsigned_value {
    ($($type:ty),+ $(,)?) => {
        $(
            impl ContractValue for $type {
                fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
                    <$type>::try_from(decoder.u64().map_err(ContractError::codec)?)
                        .map_err(|_| ContractError::new("Unsigned integer is out of range"))
                }

                fn encode_value(&self, encoder: &mut Encoder) {
                    encoder.u64(u64::from(*self));
                }
            }
        )+
    };
}

unsigned_value!(u16, u32);

impl ContractValue for u64 {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        decoder.u64().map_err(ContractError::codec)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.u64(*self);
    }
}

impl ContractValue for u128 {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        decoder.amount().map_err(ContractError::codec)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.amount(*self);
    }
}

macro_rules! signed_value {
    ($($type:ty),+ $(,)?) => {
        $(
            impl ContractValue for $type {
                fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
                    <$type>::try_from(decoder.i64().map_err(ContractError::codec)?)
                        .map_err(|_| ContractError::new("Signed integer is out of range"))
                }

                fn encode_value(&self, encoder: &mut Encoder) {
                    encoder.i64(i64::from(*self));
                }
            }
        )+
    };
}

signed_value!(i8, i16, i32, i64);

impl ContractValue for String {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        decoder.string().map_err(ContractError::codec)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.string(self);
    }
}

impl<T: ContractValue> ContractValue for Vec<T> {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        let count = decoder.array().map_err(ContractError::codec)?;
        if count > MAX_COLLECTION_ENTRIES {
            return Err(ContractError::new("Contract collection is too large"));
        }
        let mut values = Vec::with_capacity(count as usize);
        for _ in 0..count {
            values.push(T::decode_value(decoder)?);
        }
        Ok(values)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.array(self.len() as u32);
        for value in self {
            value.encode_value(encoder);
        }
    }
}

impl<T: ContractValue> ContractValue for Option<T> {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        match decoder.array().map_err(ContractError::codec)? {
            1 if decoder.u64().map_err(ContractError::codec)? == 0 => Ok(None),
            2 if decoder.u64().map_err(ContractError::codec)? == 1 => {
                Ok(Some(T::decode_value(decoder)?))
            }
            _ => Err(ContractError::new("Invalid optional value")),
        }
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        match self {
            None => {
                encoder.array(1);
                encoder.u64(0);
            }
            Some(value) => {
                encoder.array(2);
                encoder.u64(1);
                value.encode_value(encoder);
            }
        }
    }
}

macro_rules! tuple_value {
    ($length:literal, $($index:tt => $name:ident),+ $(,)?) => {
        impl<$($name: ContractValue),+> ContractValue for ($($name,)+) {
            fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
                if decoder.array().map_err(ContractError::codec)? != $length {
                    return Err(ContractError::new("Invalid tuple value"));
                }
                Ok(($($name::decode_value(decoder)?,)+))
            }

            fn encode_value(&self, encoder: &mut Encoder) {
                encoder.array($length);
                $(self.$index.encode_value(encoder);)+
            }
        }
    };
}

tuple_value!(1, 0 => A);
tuple_value!(2, 0 => A, 1 => B);
tuple_value!(3, 0 => A, 1 => B, 2 => C);
tuple_value!(4, 0 => A, 1 => B, 2 => C, 3 => D);
tuple_value!(5, 0 => A, 1 => B, 2 => C, 3 => D, 4 => E);

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct ActorId(String);

impl ActorId {
    pub fn new(value: String) -> ContractResult<Self> {
        if value.is_empty()
            || value.len() > 40
            || !value
                .bytes()
                .all(|value| value.is_ascii_hexdigit() && !value.is_ascii_uppercase())
        {
            return Err(ContractError::new("Actor ID is invalid"));
        }
        let mut normalized = String::with_capacity(40);
        normalized.extend(core::iter::repeat_n('0', 40 - value.len()));
        normalized.push_str(&value);
        Ok(Self(normalized))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }

    #[must_use]
    pub fn into_string(self) -> String {
        self.0
    }
}

impl ContractValue for ActorId {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        Self::new(String::decode_value(decoder)?)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.string(&self.0);
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct NonZeroAmount(u128);

impl NonZeroAmount {
    pub fn new(value: u128) -> ContractResult<Self> {
        if value == 0 {
            Err(ContractError::new("Amount must be greater than zero"))
        } else {
            Ok(Self(value))
        }
    }

    #[must_use]
    pub fn get(self) -> u128 {
        self.0
    }
}

impl ContractValue for NonZeroAmount {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        Self::new(u128::decode_value(decoder)?)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.amount(self.0);
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundedString<const MAX: usize>(String);

impl<const MAX: usize> BoundedString<MAX> {
    pub fn new(value: String) -> ContractResult<Self> {
        if value.is_empty() || value.len() > MAX {
            return Err(ContractError::new("Text length is out of range"));
        }
        Ok(Self(value))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }

    #[must_use]
    pub fn into_string(self) -> String {
        self.0
    }
}

impl<const MAX: usize> ContractValue for BoundedString<MAX> {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        Self::new(String::decode_value(decoder)?)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.string(&self.0);
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StateMap<K, V, const MAX: usize>(Vec<(K, V)>);

impl<K, V, const MAX: usize> Default for StateMap<K, V, MAX> {
    fn default() -> Self {
        Self(Vec::new())
    }
}

impl<K: Ord, V, const MAX: usize> StateMap<K, V, MAX> {
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.0
            .binary_search_by(|(candidate, _)| candidate.cmp(key))
            .is_ok()
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.0
            .binary_search_by(|(candidate, _)| candidate.cmp(key))
            .ok()
            .map(|index| &self.0[index].1)
    }

    pub fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        self.0
            .binary_search_by(|(candidate, _)| candidate.cmp(key))
            .ok()
            .map(|index| &mut self.0[index].1)
    }

    pub fn insert(&mut self, key: K, value: V) -> ContractResult<Option<V>> {
        match self
            .0
            .binary_search_by(|(candidate, _)| candidate.cmp(&key))
        {
            Ok(index) => Ok(Some(core::mem::replace(&mut self.0[index].1, value))),
            Err(index) => {
                if self.0.len() >= MAX {
                    return Err(ContractError::new("Contract map is full"));
                }
                self.0.insert(index, (key, value));
                Ok(None)
            }
        }
    }

    pub fn remove(&mut self, key: &K) -> Option<V> {
        self.0
            .binary_search_by(|(candidate, _)| candidate.cmp(key))
            .ok()
            .map(|index| self.0.remove(index).1)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.0.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    pub fn iter(&self) -> impl ExactSizeIterator<Item = (&K, &V)> {
        self.0.iter().map(|(key, value)| (key, value))
    }

    pub fn keys(&self) -> impl ExactSizeIterator<Item = &K> {
        self.0.iter().map(|(key, _)| key)
    }

    pub fn values(&self) -> impl ExactSizeIterator<Item = &V> {
        self.0.iter().map(|(_, value)| value)
    }
}

impl<K: ContractValue + Ord, V: ContractValue, const MAX: usize> ContractValue
    for StateMap<K, V, MAX>
{
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        let count = decoder.array().map_err(ContractError::codec)? as usize;
        if count > MAX {
            return Err(ContractError::new("Contract map is too large"));
        }
        let mut values = Self::default();
        for _ in 0..count {
            if decoder.array().map_err(ContractError::codec)? != 2 {
                return Err(ContractError::new("Invalid contract map entry"));
            }
            let key = K::decode_value(decoder)?;
            let value = V::decode_value(decoder)?;
            if values.insert(key, value)?.is_some() {
                return Err(ContractError::new("Contract map contains a duplicate key"));
            }
        }
        Ok(values)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.array(self.0.len() as u32);
        for (key, value) in &self.0 {
            encoder.array(2);
            key.encode_value(encoder);
            value.encode_value(encoder);
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StateSet<T, const MAX: usize>(Vec<T>);

impl<T, const MAX: usize> Default for StateSet<T, MAX> {
    fn default() -> Self {
        Self(Vec::new())
    }
}

impl<T: Ord, const MAX: usize> StateSet<T, MAX> {
    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.0.binary_search(value).is_ok()
    }

    pub fn insert(&mut self, value: T) -> ContractResult<bool> {
        match self.0.binary_search(&value) {
            Ok(_) => Ok(false),
            Err(index) => {
                if self.0.len() >= MAX {
                    return Err(ContractError::new("Contract set is full"));
                }
                self.0.insert(index, value);
                Ok(true)
            }
        }
    }

    pub fn remove(&mut self, value: &T) -> bool {
        self.0
            .binary_search(value)
            .ok()
            .map(|index| self.0.remove(index))
            .is_some()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.0.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    pub fn iter(&self) -> impl ExactSizeIterator<Item = &T> {
        self.0.iter()
    }
}

impl<T: ContractValue + Ord, const MAX: usize> ContractValue for StateSet<T, MAX> {
    fn decode_value(decoder: &mut Decoder<'_>) -> ContractResult<Self> {
        let count = decoder.array().map_err(ContractError::codec)? as usize;
        if count > MAX {
            return Err(ContractError::new("Contract set is too large"));
        }
        let mut values = Self::default();
        for _ in 0..count {
            if !values.insert(T::decode_value(decoder)?)? {
                return Err(ContractError::new(
                    "Contract set contains a duplicate value",
                ));
            }
        }
        Ok(values)
    }

    fn encode_value(&self, encoder: &mut Encoder) {
        encoder.array(self.0.len() as u32);
        for value in &self.0 {
            value.encode_value(encoder);
        }
    }
}

pub trait ContractState: Default {
    const VERSION: u64;
    fn decode_state(source: &[u8]) -> ContractResult<Self>;
    fn encode_state(&self) -> Vec<u8>;
}

pub trait OwnedContract {
    fn contract_owner(&self) -> &str;
}

#[must_use]
pub fn encode_result<T: ContractValue>(value: &T) -> Vec<u8> {
    let mut encoder = Encoder::new();
    value.encode_value(&mut encoder);
    encoder.finish()
}

#[macro_export]
macro_rules! require {
    ($condition:expr, $error:expr $(,)?) => {
        if !$condition {
            return Err(($error).into());
        }
    };
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InvokeRequest {
    pub sender: String,
    pub caller: String,
    pub contract_id: String,
    pub method: String,
    pub arguments: Vec<u8>,
    pub state: Vec<u8>,
    pub block: u64,
    pub depth: u64,
    pub verified: VerifiedInputs,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DagProof {
    pub transaction_hash: String,
    pub section: u64,
    pub confirmations: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DfsProof {
    pub file_id: String,
    pub owner_id: String,
    pub content_hash: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct VerifiedInputs {
    pub dag: Vec<DagProof>,
    pub dfs: Vec<DfsProof>,
}

impl InvokeRequest {
    pub fn decode(source: &[u8]) -> Result<Self, CodecError> {
        let mut decoder = Decoder::new(source);
        if decoder.array()? != 6 || decoder.array()? != 5 {
            return Err(CodecError::InvalidData);
        }

        let sender = decoder.string()?;
        let caller = decoder.string()?;
        let contract_id = decoder.string()?;
        let block = decoder.u64()?;
        let depth = decoder.u64()?;
        let method = decoder.string()?;
        let arguments = decoder.bytes()?;
        let state = decoder.bytes()?;
        let verified = VerifiedInputs::decode(&mut decoder)?;
        let version = decoder.u64()?;
        if version != u64::from(ABI_VERSION) || !decoder.is_empty() {
            return Err(CodecError::InvalidData);
        }

        Ok(Self {
            sender,
            caller,
            contract_id,
            method,
            arguments,
            state,
            block,
            depth,
            verified,
        })
    }

    #[must_use]
    pub fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(6);
        encoder.array(5);
        encoder.string(&self.sender);
        encoder.string(&self.caller);
        encoder.string(&self.contract_id);
        encoder.u64(self.block);
        encoder.u64(self.depth);
        encoder.string(&self.method);
        encoder.bytes(&self.arguments);
        encoder.bytes(&self.state);
        self.verified.encode(&mut encoder);
        encoder.u64(u64::from(ABI_VERSION));
        encoder.finish()
    }
}

impl VerifiedInputs {
    fn decode(decoder: &mut Decoder<'_>) -> Result<Self, CodecError> {
        if decoder.array()? != 2 {
            return Err(CodecError::InvalidData);
        }
        let dag_count = decoder.array()?;
        if dag_count > MAX_PROOFS {
            return Err(CodecError::LimitExceeded);
        }
        let mut dag = Vec::with_capacity(dag_count as usize);
        for _ in 0..dag_count {
            if decoder.array()? != 3 {
                return Err(CodecError::InvalidData);
            }
            dag.push(DagProof {
                transaction_hash: decoder.string()?,
                section: decoder.u64()?,
                confirmations: decoder.u64()?,
            });
        }
        let dfs_count = decoder.array()?;
        if dfs_count > MAX_PROOFS || dag_count.saturating_add(dfs_count) > MAX_PROOFS {
            return Err(CodecError::LimitExceeded);
        }
        let mut dfs = Vec::with_capacity(dfs_count as usize);
        for _ in 0..dfs_count {
            if decoder.array()? != 3 {
                return Err(CodecError::InvalidData);
            }
            dfs.push(DfsProof {
                file_id: decoder.string()?,
                owner_id: decoder.string()?,
                content_hash: decoder.string()?,
            });
        }
        Ok(Self { dag, dfs })
    }

    fn encode(&self, encoder: &mut Encoder) {
        encoder.array(2);
        encoder.array(self.dag.len() as u32);
        for proof in &self.dag {
            encoder.array(3);
            encoder.string(&proof.transaction_hash);
            encoder.u64(proof.section);
            encoder.u64(proof.confirmations);
        }
        encoder.array(self.dfs.len() as u32);
        for proof in &self.dfs {
            encoder.array(3);
            encoder.string(&proof.file_id);
            encoder.string(&proof.owner_id);
            encoder.string(&proof.content_hash);
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Event {
    pub topic: String,
    pub data: Vec<u8>,
}

impl Event {
    #[must_use]
    pub fn new<T: ContractValue>(topic: &str, data: &T) -> Self {
        Self {
            topic: topic.to_string(),
            data: encode_result(data),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Effect {
    ContractCall {
        contract_id: String,
        method: String,
        arguments: Vec<u8>,
    },
    TokenDelta {
        token_id: String,
        operation: String,
        arguments: Vec<u8>,
    },
    DfsWrite {
        owner_id: String,
        name: String,
        arguments: Vec<u8>,
    },
}

impl Effect {
    #[must_use]
    pub fn contract_call<T: ContractValue>(
        contract_id: &ActorId,
        method: &BoundedString<64>,
        arguments: &T,
    ) -> Self {
        Self::ContractCall {
            contract_id: contract_id.as_str().to_string(),
            method: method.as_str().to_string(),
            arguments: encode_result(arguments),
        }
    }

    #[must_use]
    pub fn token_delta<T: ContractValue>(token_id: &str, operation: &str, data: &T) -> Self {
        Self::TokenDelta {
            token_id: token_id.to_string(),
            operation: operation.to_string(),
            arguments: encode_result(data),
        }
    }

    #[must_use]
    pub fn dfs_write<T: ContractValue>(owner_id: &ActorId, operation: &str, data: &T) -> Self {
        Self::DfsWrite {
            owner_id: owner_id.as_str().to_string(),
            name: operation.to_string(),
            arguments: encode_result(data),
        }
    }
}

#[must_use]
pub fn is_content_hash(value: &str, allow_empty: bool) -> bool {
    (allow_empty && value.is_empty())
        || (value.len() == 64
            && value
                .bytes()
                .all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()))
}

#[must_use]
pub fn is_dfs_logical_key(value: &str) -> bool {
    if value.is_empty() || value.len() > 128 || value.starts_with('/') || value.ends_with('/') {
        return false;
    }
    if value == ".." || value.starts_with("../") || value.ends_with("/..") || value.contains("/../")
    {
        return false;
    }
    value
        .bytes()
        .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'/'))
}

pub struct Context<'a> {
    request: &'a InvokeRequest,
    events: Vec<Event>,
    effects: Vec<Effect>,
}

impl<'a> Context<'a> {
    #[must_use]
    pub fn new(request: &'a InvokeRequest) -> Self {
        Self {
            request,
            events: Vec::new(),
            effects: Vec::new(),
        }
    }

    #[must_use]
    pub fn sender(&self) -> &str {
        &self.request.sender
    }

    #[must_use]
    pub fn caller(&self) -> &str {
        &self.request.caller
    }

    #[must_use]
    pub fn contract_id(&self) -> &str {
        &self.request.contract_id
    }

    #[must_use]
    pub fn block(&self) -> u64 {
        self.request.block
    }

    #[must_use]
    pub fn depth(&self) -> u64 {
        self.request.depth
    }

    #[must_use]
    pub fn verified(&self) -> &VerifiedInputs {
        &self.request.verified
    }

    #[must_use]
    pub fn dag_proof(
        &self,
        transaction_hash: &str,
        minimum_confirmations: u64,
    ) -> Option<&DagProof> {
        self.request.verified.dag.iter().find(|proof| {
            proof.transaction_hash == transaction_hash
                && proof.confirmations >= minimum_confirmations
        })
    }

    #[must_use]
    pub fn dfs_proof(
        &self,
        owner_id: &str,
        file_id: &str,
        content_hash: &str,
    ) -> Option<&DfsProof> {
        self.request.verified.dfs.iter().find(|proof| {
            proof.owner_id == owner_id
                && proof.file_id == file_id
                && proof.content_hash == content_hash
        })
    }

    #[must_use]
    pub fn has_dfs_proof(&self, owner_id: &str, file_id: &str, content_hash: &str) -> bool {
        self.dfs_proof(owner_id, file_id, content_hash).is_some()
    }

    pub fn emit<T: ContractValue>(&mut self, topic: &str, data: &T) {
        self.events.push(Event::new(topic, data));
    }

    pub fn token_event<T: ContractValue>(&mut self, operation: &str, data: &T) {
        self.events.push(Event::new(operation, data));
        self.effects.push(Effect::token_delta(
            &self.request.contract_id,
            operation,
            data,
        ));
    }

    pub fn fungible_mint(&mut self, receiver: &ActorId, amount: NonZeroAmount) {
        self.token_event(
            "mint",
            &alloc::vec![(receiver.as_str().to_string(), amount.get())],
        );
    }

    pub fn fungible_burn(&mut self, owner: &ActorId, amount: NonZeroAmount) {
        self.token_event(
            "burn",
            &alloc::vec![(owner.as_str().to_string(), amount.get())],
        );
    }

    pub fn fungible_transfer(
        &mut self,
        sender: &ActorId,
        receiver: &ActorId,
        amount: NonZeroAmount,
    ) {
        self.token_event(
            "transfer",
            &alloc::vec![
                (sender.as_str().to_string(), amount.get()),
                (receiver.as_str().to_string(), amount.get()),
            ],
        );
    }

    pub fn fungible_lock(&mut self, owner: &ActorId, amount: NonZeroAmount) {
        self.token_event(
            "lock",
            &alloc::vec![(owner.as_str().to_string(), amount.get())],
        );
    }

    pub fn nft_mint(&mut self, token_id: u128, receiver: &ActorId) {
        self.token_event("nft_mint", &(token_id, receiver.as_str().to_string()));
    }

    pub fn nft_transfer(&mut self, token_id: u128, sender: &ActorId, receiver: &ActorId) {
        self.token_event(
            "nft_transfer",
            &(
                token_id,
                sender.as_str().to_string(),
                receiver.as_str().to_string(),
            ),
        );
    }

    pub fn nft_burn(&mut self, token_id: u128, owner: &ActorId) {
        self.token_event("nft_burn", &(token_id, owner.as_str().to_string()));
    }

    pub fn call<T: ContractValue>(&mut self, contract_id: &str, method: &str, arguments: &T) {
        self.effects.push(Effect::ContractCall {
            contract_id: contract_id.to_string(),
            method: method.to_string(),
            arguments: encode_result(arguments),
        });
    }

    pub fn call_contract<T: ContractValue>(
        &mut self,
        contract_id: &ActorId,
        method: &BoundedString<64>,
        arguments: &T,
    ) {
        self.effects
            .push(Effect::contract_call(contract_id, method, arguments));
    }

    pub fn dfs_write<T: ContractValue>(&mut self, owner_id: &str, name: &str, arguments: &T) {
        self.effects.push(Effect::DfsWrite {
            owner_id: owner_id.to_string(),
            name: name.to_string(),
            arguments: encode_result(arguments),
        });
    }

    pub fn dfs_bind(
        &mut self,
        owner_id: &ActorId,
        logical_key: &str,
        file_id: &str,
        content_hash: &str,
        previous_content_hash: &str,
    ) -> ContractResult<()> {
        if !is_dfs_logical_key(logical_key)
            || file_id.is_empty()
            || !is_content_hash(content_hash, false)
            || !is_content_hash(previous_content_hash, true)
            || !self.has_dfs_proof(owner_id.as_str(), file_id, content_hash)
        {
            return Err(ContractError::new("Invalid ExDFS binding"));
        }
        self.effects.push(Effect::dfs_write(
            owner_id,
            "bind",
            &(
                logical_key.to_string(),
                file_id.to_string(),
                content_hash.to_string(),
                previous_content_hash.to_string(),
            ),
        ));
        Ok(())
    }

    pub fn dfs_tombstone(
        &mut self,
        owner_id: &ActorId,
        logical_key: &str,
        previous_content_hash: &str,
    ) -> ContractResult<()> {
        if !is_dfs_logical_key(logical_key) || !is_content_hash(previous_content_hash, false) {
            return Err(ContractError::new("Invalid ExDFS tombstone"));
        }
        self.effects.push(Effect::dfs_write(
            owner_id,
            "tombstone",
            &(
                logical_key.to_string(),
                String::new(),
                String::new(),
                previous_content_hash.to_string(),
            ),
        ));
        Ok(())
    }

    #[must_use]
    pub fn finish(self) -> (Vec<Event>, Vec<Effect>) {
        (self.events, self.effects)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InvokeResponse {
    pub ok: bool,
    pub state: Vec<u8>,
    pub data: Vec<u8>,
    pub events: Vec<Event>,
    pub effects: Vec<Effect>,
    pub error: Option<String>,
}

impl InvokeResponse {
    #[must_use]
    pub fn success(state: Vec<u8>, data: Vec<u8>, events: Vec<Event>) -> Self {
        Self {
            ok: true,
            state,
            data,
            events,
            effects: Vec::new(),
            error: None,
        }
    }

    #[must_use]
    pub fn failure(state: Vec<u8>, error: impl ToString) -> Self {
        Self {
            ok: false,
            state,
            data: Vec::new(),
            events: Vec::new(),
            effects: Vec::new(),
            error: Some(error.to_string()),
        }
    }

    #[must_use]
    pub fn with_effects(mut self, effects: Vec<Effect>) -> Self {
        self.effects = effects;
        self
    }

    #[must_use]
    pub fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(6);
        encoder.boolean(self.ok);
        encoder.bytes(&self.state);
        encoder.bytes(&self.data);
        encoder.array(self.events.len() as u32);
        for event in &self.events {
            encoder.array(2);
            encoder.string(&event.topic);
            encoder.bytes(&event.data);
        }
        encoder.array(self.effects.len() as u32);
        for effect in &self.effects {
            match effect {
                Effect::ContractCall {
                    contract_id,
                    method,
                    arguments,
                } => {
                    encoder.array(4);
                    encoder.string("contract_call");
                    encoder.string(contract_id);
                    encoder.string(method);
                    encoder.bytes(arguments);
                }
                Effect::TokenDelta {
                    token_id,
                    operation,
                    arguments,
                } => {
                    encoder.array(4);
                    encoder.string("token_delta");
                    encoder.string(token_id);
                    encoder.string(operation);
                    encoder.bytes(arguments);
                }
                Effect::DfsWrite {
                    owner_id,
                    name,
                    arguments,
                } => {
                    encoder.array(4);
                    encoder.string("dfs_write");
                    encoder.string(owner_id);
                    encoder.string(name);
                    encoder.bytes(arguments);
                }
            }
        }
        match &self.error {
            Some(error) => encoder.string(error),
            None => encoder.nil(),
        }
        encoder.finish()
    }
}

pub trait Contract {
    fn invoke(request: InvokeRequest) -> InvokeResponse;
}

#[macro_export]
macro_rules! export_contract {
    ($contract:ty) => {
        #[cfg(target_family = "wasm")]
        mod extrachain_abi {
            use super::*;
            use alloc::vec::Vec;
            use core::sync::atomic::{AtomicU32, Ordering};

            use $crate::{Contract, InvokeRequest, InvokeResponse};

            static RESULT_LENGTH: AtomicU32 = AtomicU32::new(0);

            #[used]
            #[unsafe(link_section = "extrachain.language")]
            static CONTRACT_LANGUAGE: [u8; 4] = *b"rust";

            #[unsafe(no_mangle)]
            pub unsafe extern "C" fn exc_invoke(pointer: u32, length: u32) -> u32 {
                let input =
                    unsafe { core::slice::from_raw_parts(pointer as *const u8, length as usize) };
                let response = match InvokeRequest::decode(input) {
                    Ok(request) => <$contract as Contract>::invoke(request),
                    Err(_) => InvokeResponse::failure(Vec::new(), "Invalid contract request"),
                };
                let output = response.encode();
                let pointer = output.as_ptr() as u32;
                RESULT_LENGTH.store(output.len() as u32, Ordering::Release);
                core::mem::forget(output);
                pointer
            }

            #[unsafe(no_mangle)]
            pub extern "C" fn exc_result_len() -> u32 {
                RESULT_LENGTH.load(Ordering::Acquire)
            }
        }
    };
}

#[cfg(all(target_family = "wasm", not(test)))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    core::arch::wasm32::unreachable()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unsigned_decoder_accepts_all_message_pack_widths() {
        for (encoded, expected) in [
            (&[0x7f][..], 127),
            (&[0xcc, 0x80][..], 128),
            (&[0xcd, 0x01, 0x00][..], 256),
            (&[0xce, 0x00, 0x01, 0x00, 0x00][..], 65_536),
            (
                &[0xcf, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00][..],
                4_294_967_296,
            ),
        ] {
            let mut decoder = Decoder::new(encoded);
            assert_eq!(decoder.u64(), Ok(expected));
            assert!(decoder.is_empty());
        }
    }

    #[test]
    fn unsigned_decoder_rejects_float_values() {
        let mut decoder = Decoder::new(&[0xca, 0x3f, 0x80, 0x00, 0x00]);
        assert_eq!(decoder.u64(), Err(CodecError::InvalidData));
    }

    #[test]
    fn signed_values_and_optional_values_round_trip() {
        let values = (
            i64::MIN,
            i32::MAX,
            Some(7_u64),
            Option::<()>::None,
            Some(()),
        );
        let encoded = encode_result(&values);
        let mut decoder = Decoder::new(&encoded);
        assert_eq!(
            <(i64, i32, Option<u64>, Option<()>, Option<()>)>::decode_value(&mut decoder).unwrap(),
            values
        );
        assert!(decoder.is_empty());
    }

    #[test]
    fn actor_id_matches_core_normalization_rules() {
        let actor = ActorId::new("abc".to_string()).unwrap();
        assert_eq!(actor.as_str(), "0000000000000000000000000000000000000abc");
        assert!(ActorId::new("ABC".to_string()).is_err());
        assert!(ActorId::new("xyz".to_string()).is_err());
        assert!(ActorId::new("1".repeat(41)).is_err());
    }

    #[test]
    fn amount_round_trips_full_u128_range() {
        let mut encoder = Encoder::new();
        encoder.amount(u128::MAX);
        let encoded = encoder.finish();
        let mut decoder = Decoder::new(&encoded);
        assert_eq!(decoder.amount(), Ok(u128::MAX));
        assert!(decoder.is_empty());
    }

    #[test]
    fn amount_rejects_non_canonical_decimal_text() {
        let mut encoder = Encoder::new();
        encoder.string("01");
        let encoded = encoder.finish();
        let mut decoder = Decoder::new(&encoded);
        assert_eq!(decoder.amount(), Err(CodecError::InvalidData));
    }

    #[test]
    fn current_abi_request_round_trips_verified_inputs() {
        let request = InvokeRequest {
            sender: "alice".to_string(),
            caller: "parent".to_string(),
            contract_id: "child".to_string(),
            method: "run".to_string(),
            arguments: alloc::vec![1, 2],
            state: alloc::vec![3],
            block: 12,
            depth: 2,
            verified: VerifiedInputs {
                dag: alloc::vec![DagProof {
                    transaction_hash: "tx".to_string(),
                    section: 10,
                    confirmations: 2,
                }],
                dfs: alloc::vec![DfsProof {
                    file_id: "file".to_string(),
                    owner_id: "owner".to_string(),
                    content_hash: "hash".to_string(),
                }],
            },
        };
        assert_eq!(InvokeRequest::decode(&request.encode()), Ok(request));
    }

    #[test]
    fn response_encodes_declared_effects() {
        let response =
            InvokeResponse::success(Vec::new(), Vec::new(), Vec::new()).with_effects(alloc::vec![
                Effect::ContractCall {
                    contract_id: "child".to_string(),
                    method: "run".to_string(),
                    arguments: alloc::vec![1],
                }
            ]);
        let encoded = response.encode();
        assert!(!encoded.is_empty());
    }

    #[test]
    fn request_rejects_too_many_proofs_before_allocation() {
        let request = InvokeRequest {
            sender: "alice".to_string(),
            caller: "alice".to_string(),
            contract_id: "contract".to_string(),
            method: "run".to_string(),
            arguments: Vec::new(),
            state: Vec::new(),
            block: 1,
            depth: 0,
            verified: VerifiedInputs {
                dag: (0..=MAX_PROOFS)
                    .map(|index| DagProof {
                        transaction_hash: index.to_string(),
                        section: 0,
                        confirmations: 1,
                    })
                    .collect(),
                dfs: Vec::new(),
            },
        };
        assert_eq!(
            InvokeRequest::decode(&request.encode()),
            Err(CodecError::LimitExceeded)
        );
    }

    #[test]
    fn state_map_encoding_is_independent_of_insertion_order() {
        let mut first = StateMap::<String, u64, 4>::default();
        first.insert("beta".to_string(), 2).unwrap();
        first.insert("alpha".to_string(), 1).unwrap();
        let mut second = StateMap::<String, u64, 4>::default();
        second.insert("alpha".to_string(), 1).unwrap();
        second.insert("beta".to_string(), 2).unwrap();

        assert_eq!(encode_result(&first), encode_result(&second));
    }

    #[test]
    fn state_map_rejects_duplicate_encoded_keys() {
        let mut encoder = Encoder::new();
        encoder.array(2);
        for value in [1, 2] {
            encoder.array(2);
            encoder.string("same");
            encoder.u64(value);
        }
        let encoded = encoder.finish();
        let mut decoder = Decoder::new(&encoded);

        assert!(StateMap::<String, u64, 4>::decode_value(&mut decoder).is_err());
    }

    #[test]
    fn state_map_enforces_its_entry_limit() {
        let mut values = StateMap::<String, u64, 1>::default();
        assert!(values.insert("first".to_string(), 1).is_ok());
        assert!(values.insert("second".to_string(), 2).is_err());
    }

    #[test]
    fn state_map_mutation_and_iteration_keep_sorted_order() {
        let mut values = StateMap::<String, u64, 3>::default();
        values.insert("beta".to_string(), 2).unwrap();
        values.insert("alpha".to_string(), 1).unwrap();
        *values.get_mut(&"beta".to_string()).unwrap() = 3;
        assert!(values.contains_key(&"alpha".to_string()));
        assert_eq!(
            values
                .iter()
                .map(|(key, value)| (key.as_str(), *value))
                .collect::<Vec<_>>(),
            alloc::vec![("alpha", 1), ("beta", 3)]
        );
    }

    #[test]
    fn state_set_is_bounded_sorted_and_duplicate_safe() {
        let mut values = StateSet::<String, 2>::default();
        assert_eq!(values.insert("beta".to_string()), Ok(true));
        assert_eq!(values.insert("alpha".to_string()), Ok(true));
        assert_eq!(values.insert("alpha".to_string()), Ok(false));
        assert!(values.insert("gamma".to_string()).is_err());
        assert_eq!(
            values.iter().map(String::as_str).collect::<Vec<_>>(),
            alloc::vec!["alpha", "beta"]
        );
        let encoded = encode_result(&values);
        let mut decoder = Decoder::new(&encoded);
        assert_eq!(
            StateSet::<String, 2>::decode_value(&mut decoder),
            Ok(values)
        );
    }

    #[test]
    fn context_proof_and_effect_helpers_use_checked_shapes() {
        let owner = ActorId::new("1".to_string()).unwrap();
        let receiver = ActorId::new("2".to_string()).unwrap();
        let content_hash = "a".repeat(64);
        let request = InvokeRequest {
            sender: owner.as_str().to_string(),
            caller: owner.as_str().to_string(),
            contract_id: owner.as_str().to_string(),
            method: "run".to_string(),
            arguments: Vec::new(),
            state: Vec::new(),
            block: 12,
            depth: 0,
            verified: VerifiedInputs {
                dag: alloc::vec![DagProof {
                    transaction_hash: "tx".to_string(),
                    section: 10,
                    confirmations: 2,
                }],
                dfs: alloc::vec![DfsProof {
                    file_id: "file".to_string(),
                    owner_id: owner.as_str().to_string(),
                    content_hash: content_hash.clone(),
                }],
            },
        };
        let mut context = Context::new(&request);
        assert!(context.dag_proof("tx", 2).is_some());
        assert!(context.dag_proof("tx", 3).is_none());
        context.fungible_transfer(&owner, &receiver, NonZeroAmount::new(5).unwrap());
        assert!(
            context
                .dfs_bind(&owner, "profile/avatar", "file", &content_hash, "")
                .is_ok()
        );
        assert!(
            context
                .dfs_bind(&owner, "../unsafe", "file", &content_hash, "")
                .is_err()
        );
        let (events, effects) = context.finish();
        assert_eq!(events.len(), 1);
        assert_eq!(effects.len(), 2);
    }
}
