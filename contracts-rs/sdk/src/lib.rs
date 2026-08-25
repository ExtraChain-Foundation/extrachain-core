#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::convert::Infallible;

use rmp::decode::{RmpRead, read_array_len, read_bin_len, read_bool, read_int, read_str_len};
use rmp::encode::{write_array_len, write_bin, write_bool, write_nil, write_str, write_u64};

pub const ABI_VERSION: u32 = 2;
pub const MAX_PROOFS: u32 = 64;

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

    pub fn boolean(&mut self) -> Result<bool, CodecError> {
        read_bool(&mut self.source).map_err(|_| CodecError::InvalidData)
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Effect {
    ContractCall {
        contract_id: String,
        method: String,
        arguments: Vec<u8>,
    },
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
    fn abi_two_request_round_trips_verified_inputs() {
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
}
