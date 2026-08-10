use proc_macro::TokenStream;
use quote::quote;
use syn::{
    Attribute, Data, DeriveInput, Fields, FnArg, ImplItem, ItemImpl, LitInt, Pat, ReturnType, Type,
    parse_macro_input,
};

fn take_attribute(attributes: &mut Vec<Attribute>, name: &str) -> bool {
    let found = attributes
        .iter()
        .any(|attribute| attribute.path().is_ident(name));
    attributes.retain(|attribute| !attribute.path().is_ident(name));
    found
}

fn state_version(input: &DeriveInput) -> syn::Result<u64> {
    let mut version = None;
    for attribute in &input.attrs {
        if !attribute.path().is_ident("state") {
            continue;
        }
        attribute.parse_nested_meta(|meta| {
            if meta.path.is_ident("version") {
                let value: LitInt = meta.value()?.parse()?;
                version = Some(value.base10_parse()?);
                Ok(())
            } else {
                Err(meta.error("unsupported state option"))
            }
        })?;
    }
    version.ok_or_else(|| syn::Error::new_spanned(input, "missing #[state(version = N)]"))
}

fn named_fields(input: &DeriveInput) -> syn::Result<&syn::FieldsNamed> {
    match &input.data {
        Data::Struct(data) => match &data.fields {
            Fields::Named(fields) => Ok(fields),
            _ => Err(syn::Error::new_spanned(
                input,
                "contract codecs require a struct with named fields",
            )),
        },
        _ => Err(syn::Error::new_spanned(
            input,
            "contract codecs can only be derived for structs",
        )),
    }
}

#[proc_macro_derive(ContractCodec)]
pub fn derive_contract_codec(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let fields = match named_fields(&input) {
        Ok(fields) => fields,
        Err(error) => return error.into_compile_error().into(),
    };
    let name = &input.ident;
    let field_names: Vec<_> = fields
        .named
        .iter()
        .filter_map(|field| field.ident.as_ref())
        .collect();
    let field_types: Vec<_> = fields.named.iter().map(|field| &field.ty).collect();
    let field_count = field_names.len() as u32;
    quote! {
        impl ::extrachain_contract_sdk::ContractValue for #name {
            fn decode_value(
                decoder: &mut ::extrachain_contract_sdk::Decoder<'_>,
            ) -> ::extrachain_contract_sdk::ContractResult<Self> {
                if decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != #field_count {
                    return Err(::extrachain_contract_sdk::ContractError::new("Invalid structured value"));
                }
                Ok(Self {
                    #(
                        #field_names: <#field_types as ::extrachain_contract_sdk::ContractValue>::decode_value(decoder)?,
                    )*
                })
            }

            fn encode_value(&self, encoder: &mut ::extrachain_contract_sdk::Encoder) {
                encoder.array(#field_count);
                #(
                    ::extrachain_contract_sdk::ContractValue::encode_value(&self.#field_names, encoder);
                )*
            }
        }
    }
    .into()
}

#[proc_macro_derive(ContractState, attributes(state, owner))]
pub fn derive_contract_state(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let version = match state_version(&input) {
        Ok(version) => version,
        Err(error) => return error.into_compile_error().into(),
    };
    let fields = match named_fields(&input) {
        Ok(fields) => fields,
        Err(error) => return error.into_compile_error().into(),
    };
    let name = &input.ident;
    let field_names: Vec<_> = fields
        .named
        .iter()
        .filter_map(|field| field.ident.as_ref())
        .collect();
    let field_types: Vec<_> = fields.named.iter().map(|field| &field.ty).collect();
    let field_count = field_names.len() as u32 + 1;
    let owners: Vec<_> = fields
        .named
        .iter()
        .filter(|field| {
            field
                .attrs
                .iter()
                .any(|attribute| attribute.path().is_ident("owner"))
        })
        .filter_map(|field| field.ident.as_ref())
        .collect();
    if owners.len() > 1 {
        return syn::Error::new_spanned(fields, "only one field can use #[owner]")
            .into_compile_error()
            .into();
    }
    let owner_impl = owners.first().map(|field| {
        quote! {
            impl ::extrachain_contract_sdk::OwnedContract for #name {
                fn contract_owner(&self) -> &str {
                    self.#field.as_str()
                }
            }
        }
    });
    quote! {
        impl ::extrachain_contract_sdk::ContractState for #name {
            const VERSION: u64 = #version;

            fn decode_state(source: &[u8]) -> ::extrachain_contract_sdk::ContractResult<Self> {
                if source.is_empty() {
                    return Ok(Self::default());
                }
                let mut decoder = ::extrachain_contract_sdk::Decoder::new(source);
                if decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != #field_count {
                    return Err(::extrachain_contract_sdk::ContractError::new("Invalid contract state"));
                }
                let version = decoder.u64().map_err(::extrachain_contract_sdk::ContractError::codec)?;
                if version != Self::VERSION {
                    return Err(::extrachain_contract_sdk::ContractError::new("Unsupported contract state version"));
                }
                let state = Self {
                    #(
                        #field_names: <#field_types as ::extrachain_contract_sdk::ContractValue>::decode_value(&mut decoder)?,
                    )*
                };
                if !decoder.is_empty() {
                    return Err(::extrachain_contract_sdk::ContractError::new("Contract state has trailing data"));
                }
                Ok(state)
            }

            fn encode_state(&self) -> alloc::vec::Vec<u8> {
                let mut encoder = ::extrachain_contract_sdk::Encoder::new();
                encoder.array(#field_count);
                encoder.u64(Self::VERSION);
                #(
                    ::extrachain_contract_sdk::ContractValue::encode_value(&self.#field_names, &mut encoder);
                )*
                encoder.finish()
            }
        }

        #owner_impl
    }
    .into()
}

fn is_context(ty: &Type) -> bool {
    let Type::Reference(reference) = ty else {
        return false;
    };
    let Type::Path(path) = reference.elem.as_ref() else {
        return false;
    };
    path.path
        .segments
        .last()
        .is_some_and(|segment| segment.ident == "Context")
}

#[proc_macro_attribute]
pub fn contract(_attribute: TokenStream, input: TokenStream) -> TokenStream {
    let mut item = parse_macro_input!(input as ItemImpl);
    if item.trait_.is_some() {
        return syn::Error::new_spanned(item, "#[contract] requires an inherent impl")
            .into_compile_error()
            .into();
    }
    let self_ty = item.self_ty.clone();
    let mut arms = Vec::new();
    let mut known_methods = std::collections::BTreeSet::new();

    for impl_item in &mut item.items {
        let ImplItem::Fn(method) = impl_item else {
            continue;
        };
        let init = take_attribute(&mut method.attrs, "init");
        let call = take_attribute(&mut method.attrs, "call");
        let query = take_attribute(&mut method.attrs, "query");
        let authorize_upgrade = take_attribute(&mut method.attrs, "authorize_upgrade");
        let migrate = take_attribute(&mut method.attrs, "migrate");
        let owner_only = take_attribute(&mut method.attrs, "owner_only");
        let role_count = [init, call, query, authorize_upgrade, migrate]
            .into_iter()
            .filter(|value| *value)
            .count();
        if role_count == 0 {
            if owner_only {
                return syn::Error::new_spanned(
                    method,
                    "#[owner_only] requires an exported method",
                )
                .into_compile_error()
                .into();
            }
            continue;
        }
        if role_count != 1 {
            return syn::Error::new_spanned(
                method,
                "a contract method needs exactly one route attribute",
            )
            .into_compile_error()
            .into();
        }
        let method_name = method.sig.ident.to_string();
        if !known_methods.insert(method_name.clone()) {
            return syn::Error::new_spanned(method, "duplicate contract method")
                .into_compile_error()
                .into();
        }
        if matches!(method.sig.output, ReturnType::Default) {
            return syn::Error::new_spanned(
                method,
                "contract methods must return ContractResult<T>",
            )
            .into_compile_error()
            .into();
        }
        let mut inputs = method.sig.inputs.iter();
        let receiver = inputs.next();
        let Some(FnArg::Receiver(receiver)) = receiver else {
            return syn::Error::new_spanned(method, "contract methods require a self receiver")
                .into_compile_error()
                .into();
        };
        if (call || init || migrate) && receiver.mutability.is_none() {
            return syn::Error::new_spanned(method, "state-changing methods require &mut self")
                .into_compile_error()
                .into();
        }
        if query && receiver.mutability.is_some() {
            return syn::Error::new_spanned(method, "query methods cannot use &mut self")
                .into_compile_error()
                .into();
        }

        let mut has_context = false;
        let mut argument_names = Vec::new();
        let mut argument_types = Vec::new();
        for input in inputs {
            let FnArg::Typed(argument) = input else {
                continue;
            };
            if !has_context && is_context(&argument.ty) {
                has_context = true;
                continue;
            }
            let Pat::Ident(pattern) = argument.pat.as_ref() else {
                return syn::Error::new_spanned(
                    argument,
                    "contract arguments require simple names",
                )
                .into_compile_error()
                .into();
            };
            argument_names.push(pattern.ident.clone());
            argument_types.push(argument.ty.as_ref().clone());
        }
        let argument_count = argument_names.len() as u32;
        let method_ident = &method.sig.ident;
        let context_argument = has_context.then(|| quote! { &mut __context, });
        let owner_guard = owner_only.then(|| {
            quote! {
                let __owner = ::extrachain_contract_sdk::OwnedContract::contract_owner(&__state);
                if __owner != __context.caller() {
                    return Err(::extrachain_contract_sdk::ContractError::new("Only the owner can perform this operation"));
                }
            }
        });
        let init_guard = init.then(|| {
            quote! {
                if !request.state.is_empty() {
                    return ::extrachain_contract_sdk::InvokeResponse::failure(
                        request.state,
                        "Contract is already initialized",
                    );
                }
            }
        });
        let arm = quote! {
            #method_name => {
                #init_guard
                let __result: ::extrachain_contract_sdk::ContractResult<_> = (|| {
                    let mut __decoder = ::extrachain_contract_sdk::Decoder::new(&request.arguments);
                    if __decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != #argument_count {
                        return Err(::extrachain_contract_sdk::ContractError::new("Invalid contract arguments"));
                    }
                    #(
                        let #argument_names = <#argument_types as ::extrachain_contract_sdk::ContractValue>::decode_value(&mut __decoder)?;
                    )*
                    if !__decoder.is_empty() {
                        return Err(::extrachain_contract_sdk::ContractError::new("Contract arguments have trailing data"));
                    }
                    let mut __context = ::extrachain_contract_sdk::Context::new(&request);
                    #owner_guard
                    let __value = __state.#method_ident(#context_argument #(#argument_names),*)?;
                    let __data = ::extrachain_contract_sdk::encode_result(&__value);
                    let (__events, __effects) = __context.finish();
                    Ok(::extrachain_contract_sdk::InvokeResponse::success(
                        ::extrachain_contract_sdk::ContractState::encode_state(&__state),
                        __data,
                        __events,
                    ).with_effects(__effects))
                })();
                match __result {
                    Ok(response) => response,
                    Err(error) => ::extrachain_contract_sdk::InvokeResponse::failure(request.state, error),
                }
            }
        };
        arms.push(arm);
    }

    quote! {
        #item

        impl ::extrachain_contract_sdk::Contract for #self_ty {
            fn invoke(request: ::extrachain_contract_sdk::InvokeRequest) -> ::extrachain_contract_sdk::InvokeResponse {
                let mut __state = match <#self_ty as ::extrachain_contract_sdk::ContractState>::decode_state(&request.state) {
                    Ok(state) => state,
                    Err(error) => return ::extrachain_contract_sdk::InvokeResponse::failure(request.state, error),
                };
                match request.method.as_str() {
                    #(#arms,)*
                    _ => ::extrachain_contract_sdk::InvokeResponse::failure(
                        request.state,
                        "Unknown contract method",
                    ),
                }
            }
        }

        ::extrachain_contract_sdk::export_contract!(#self_ty);
    }
    .into()
}
