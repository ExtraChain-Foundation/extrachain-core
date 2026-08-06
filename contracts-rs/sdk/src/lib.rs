#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::convert::Infallible;

use rmp::decode::{RmpRead, read_array_len, read_bin_len, read_bool, read_int, read_str_len};
use rmp::encode::{write_array_len, write_bin, write_bool, write_nil, write_str, write_u64};

pub const ABI_VERSION: u32 = 1;

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
    pub method: String,
    pub arguments: Vec<u8>,
    pub state: Vec<u8>,
    pub block: u64,
}

impl InvokeRequest {
    pub fn decode(source: &[u8]) -> Result<Self, CodecError> {
        let mut decoder = Decoder::new(source);
        if decoder.array()? != 6 {
            return Err(CodecError::InvalidData);
        }

        let sender = decoder.string()?;
        let method = decoder.string()?;
        let arguments = decoder.bytes()?;
        let state = decoder.bytes()?;
        let block = decoder.u64()?;
        let version = decoder.u64()?;
        if version != u64::from(ABI_VERSION) || !decoder.is_empty() {
            return Err(CodecError::InvalidData);
        }

        Ok(Self {
            sender,
            method,
            arguments,
            state,
            block,
        })
    }

    #[must_use]
    pub fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(6);
        encoder.string(&self.sender);
        encoder.string(&self.method);
        encoder.bytes(&self.arguments);
        encoder.bytes(&self.state);
        encoder.u64(self.block);
        encoder.u64(u64::from(ABI_VERSION));
        encoder.finish()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Event {
    pub topic: String,
    pub data: Vec<u8>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InvokeResponse {
    pub ok: bool,
    pub state: Vec<u8>,
    pub data: Vec<u8>,
    pub events: Vec<Event>,
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
            error: Some(error.to_string()),
        }
    }

    #[must_use]
    pub fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(5);
        encoder.boolean(self.ok);
        encoder.bytes(&self.state);
        encoder.bytes(&self.data);
        encoder.array(self.events.len() as u32);
        for event in &self.events {
            encoder.array(2);
            encoder.string(&event.topic);
            encoder.bytes(&event.data);
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
