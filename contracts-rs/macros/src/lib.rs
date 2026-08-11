use proc_macro::TokenStream;
use quote::quote;
use syn::{
    Attribute, Data, DeriveInput, Fields, FnArg, ImplItem, ItemImpl, ItemStruct, LitInt, LitStr,
    Meta, Pat, ReturnType, Token, Type, parse_macro_input, punctuated::Punctuated,
};

#[derive(Clone, Copy, Default, PartialEq, Eq)]
enum Access {
    #[default]
    Public,
    Owner,
}

#[derive(Clone, Copy, Default)]
struct RouteOptions {
    access: Access,
    from: Option<u64>,
    to: Option<u64>,
}

fn take_attribute(attributes: &mut Vec<Attribute>, name: &str) -> bool {
    let found = attributes
        .iter()
        .any(|attribute| attribute.path().is_ident(name));
    attributes.retain(|attribute| !attribute.path().is_ident(name));
    found
}

fn take_route_attribute(
    attributes: &mut Vec<Attribute>,
    name: &str,
) -> syn::Result<Option<RouteOptions>> {
    let Some(index) = attributes
        .iter()
        .position(|attribute| attribute.path().is_ident(name))
    else {
        return Ok(None);
    };
    let attribute = attributes.remove(index);
    let mut options = RouteOptions::default();
    if matches!(attribute.meta, Meta::Path(_)) {
        return Ok(Some(options));
    }
    attribute.parse_nested_meta(|meta| {
        if meta.path.is_ident("access") {
            let value: LitStr = meta.value()?.parse()?;
            options.access = match value.value().as_str() {
                "public" => Access::Public,
                "owner" => Access::Owner,
                _ => return Err(meta.error("access must be \"public\" or \"owner\"")),
            };
            Ok(())
        } else if meta.path.is_ident("from") {
            let value: LitInt = meta.value()?.parse()?;
            options.from = Some(value.base10_parse()?);
            Ok(())
        } else if meta.path.is_ident("to") {
            let value: LitInt = meta.value()?.parse()?;
            options.to = Some(value.base10_parse()?);
            Ok(())
        } else {
            Err(meta.error("unsupported route option"))
        }
    })?;
    Ok(Some(options))
}

fn contract_struct(attribute: TokenStream, input: TokenStream) -> TokenStream {
    let arguments =
        parse_macro_input!(attribute with Punctuated::<Meta, Token![,]>::parse_terminated);
    let mut version = None;
    let mut owner = None;
    let mut upgrade = "locked".to_string();
    for argument in arguments {
        let Meta::NameValue(option) = argument else {
            return syn::Error::new_spanned(argument, "contract options use name = value")
                .into_compile_error()
                .into();
        };
        if option.path.is_ident("version") {
            let syn::Expr::Lit(value) = option.value else {
                return syn::Error::new_spanned(option, "version must be an integer")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Int(value) = value.lit else {
                return syn::Error::new_spanned(value, "version must be an integer")
                    .into_compile_error()
                    .into();
            };
            version = value.base10_parse::<u64>().ok();
        } else if option.path.is_ident("owner") {
            let syn::Expr::Lit(value) = option.value else {
                return syn::Error::new_spanned(option, "owner must be a field name")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Str(value) = value.lit else {
                return syn::Error::new_spanned(value, "owner must be a field name")
                    .into_compile_error()
                    .into();
            };
            owner = Some(syn::Ident::new(&value.value(), value.span()));
        } else if option.path.is_ident("upgrade") {
            let syn::Expr::Lit(value) = option.value else {
                return syn::Error::new_spanned(option, "upgrade must be \"owner\" or \"locked\"")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Str(value) = value.lit else {
                return syn::Error::new_spanned(value, "upgrade must be \"owner\" or \"locked\"")
                    .into_compile_error()
                    .into();
            };
            upgrade = value.value();
        } else {
            return syn::Error::new_spanned(option, "unsupported contract option")
                .into_compile_error()
                .into();
        }
    }
    let Some(version) = version else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing contract version")
            .into_compile_error()
            .into();
    };
    if upgrade != "owner" && upgrade != "locked" {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "upgrade must be \"owner\" or \"locked\"",
        )
        .into_compile_error()
        .into();
    }
    if upgrade == "owner" && owner.is_none() {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "owner upgrade requires an owner field",
        )
        .into_compile_error()
        .into();
    }

    let item = parse_macro_input!(input as ItemStruct);
    let Fields::Named(fields) = &item.fields else {
        return syn::Error::new_spanned(&item, "a contract requires named fields")
            .into_compile_error()
            .into();
    };
    let name = &item.ident;
    let field_names: Vec<_> = fields
        .named
        .iter()
        .filter_map(|field| field.ident.as_ref())
        .collect();
    let field_types: Vec<_> = fields.named.iter().map(|field| &field.ty).collect();
    let field_count = field_names.len() as u32 + 1;
    if let Some(owner) = &owner
        && !field_names.contains(&owner)
    {
        return syn::Error::new_spanned(owner, "owner field does not exist")
            .into_compile_error()
            .into();
    }
    let owner_value = owner.as_ref().map_or_else(
        || quote! { None },
        |field| quote! { Some(self.#field.as_str()) },
    );
    let upgrade_owner = upgrade == "owner";

    quote! {
        #item

        impl ::extrachain_contract_sdk::ContractState for #name {
            const VERSION: u64 = #version;

            fn decode_state(source: &[u8]) -> ::extrachain_contract_sdk::ContractResult<Self> {
                if source.is_empty() {
                    return Ok(Self::default());
                }
                let mut decoder = ::extrachain_contract_sdk::Decoder::new(source);
                if decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != #field_count {
                    return Err(::extrachain_contract_sdk::ContractError::state("Invalid contract state"));
                }
                let version = decoder.u64().map_err(::extrachain_contract_sdk::ContractError::codec)?;
                if version != Self::VERSION {
                    return Err(::extrachain_contract_sdk::ContractError::state("Unsupported contract state version"));
                }
                let state = Self {
                    #(
                        #field_names: <#field_types as ::extrachain_contract_sdk::ContractValue>::decode_value(&mut decoder)?,
                    )*
                };
                if !decoder.is_empty() {
                    return Err(::extrachain_contract_sdk::ContractError::state("Contract state has trailing data"));
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

        impl ::extrachain_contract_sdk::ContractConfiguration for #name {
            const OWNER_UPGRADE: bool = #upgrade_owner;

            fn configured_owner(&self) -> Option<&str> {
                #owner_value
            }
        }
    }
    .into()
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
pub fn fungible_token(attribute: TokenStream, input: TokenStream) -> TokenStream {
    let arguments =
        parse_macro_input!(attribute with Punctuated::<Meta, Token![,]>::parse_terminated);
    let mut token_name = None;
    let mut token_symbol = None;
    let mut decimals = None;
    let mut freeze_last_unit = false;
    for argument in arguments {
        let Meta::NameValue(option) = argument else {
            return syn::Error::new_spanned(argument, "token options use name = value")
                .into_compile_error()
                .into();
        };
        if option.path.is_ident("name") || option.path.is_ident("symbol") {
            let syn::Expr::Lit(value) = &option.value else {
                return syn::Error::new_spanned(option, "name and symbol must be strings")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Str(value) = &value.lit else {
                return syn::Error::new_spanned(value, "name and symbol must be strings")
                    .into_compile_error()
                    .into();
            };
            if option.path.is_ident("name") {
                token_name = Some(value.clone());
            } else {
                token_symbol = Some(value.clone());
            }
        } else if option.path.is_ident("decimals") {
            let syn::Expr::Lit(value) = &option.value else {
                return syn::Error::new_spanned(option, "decimals must be an integer")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Int(value) = &value.lit else {
                return syn::Error::new_spanned(value, "decimals must be an integer")
                    .into_compile_error()
                    .into();
            };
            decimals = value.base10_parse::<u8>().ok();
        } else if option.path.is_ident("freeze_last_unit") {
            let syn::Expr::Lit(value) = &option.value else {
                return syn::Error::new_spanned(option, "freeze_last_unit must be true or false")
                    .into_compile_error()
                    .into();
            };
            let syn::Lit::Bool(value) = &value.lit else {
                return syn::Error::new_spanned(value, "freeze_last_unit must be true or false")
                    .into_compile_error()
                    .into();
            };
            freeze_last_unit = value.value;
        } else {
            return syn::Error::new_spanned(option, "unsupported fungible token option")
                .into_compile_error()
                .into();
        }
    }
    let Some(token_name) = token_name else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing token name")
            .into_compile_error()
            .into();
    };
    let Some(token_symbol) = token_symbol else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing token symbol")
            .into_compile_error()
            .into();
    };
    let Some(decimals) = decimals else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing token decimals")
            .into_compile_error()
            .into();
    };
    if token_name.value().is_empty() || token_name.value().len() > 64 {
        return syn::Error::new_spanned(token_name, "token name must contain 1 to 64 bytes")
            .into_compile_error()
            .into();
    }
    if token_symbol.value().is_empty() || token_symbol.value().len() > 12 {
        return syn::Error::new_spanned(token_symbol, "token symbol must contain 1 to 12 bytes")
            .into_compile_error()
            .into();
    }
    if decimals > 18 {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "token decimals must be at most 18",
        )
        .into_compile_error()
        .into();
    }

    let item = parse_macro_input!(input as ItemStruct);
    let valid_shape = matches!(&item.fields, Fields::Unit)
        || matches!(&item.fields, Fields::Named(fields) if fields.named.is_empty());
    if !valid_shape {
        return syn::Error::new_spanned(item, "a standard token declaration cannot contain fields")
            .into_compile_error()
            .into();
    }
    let visibility = &item.vis;
    let name = &item.ident;
    let disable_policy = (!freeze_last_unit).then(|| {
        quote! {
            self.transfers.disable();
        }
    });

    quote! {
        #[extrachain_contract_sdk::contract(version = 2, owner = "owner", upgrade = "owner")]
        #[derive(Clone, Debug, Default, PartialEq, Eq)]
        #visibility struct #name {
            owner: alloc::string::String,
            name: alloc::string::String,
            symbol: alloc::string::String,
            decimals: u8,
            mint_enabled: bool,
            ledger: extrachain_contract_components::FungibleLedger,
            transfers: extrachain_contract_components::FreezeLastUnit,
        }

        #[extrachain_contract_sdk::contract]
        impl #name {
            #[init]
            fn init(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                supply: extrachain_contract_sdk::NonZeroAmount,
                initial_balances: alloc::vec::Vec<(
                    extrachain_contract_sdk::ActorId,
                    extrachain_contract_sdk::NonZeroAmount,
                )>,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                self.owner = alloc::string::ToString::to_string(context.caller());
                self.name = alloc::string::String::from(#token_name);
                self.symbol = alloc::string::String::from(#token_symbol);
                self.decimals = #decimals;
                self.mint_enabled = true;
                self.transfers = extrachain_contract_components::FreezeLastUnit::new(#decimals)?;
                #disable_policy
                if initial_balances.is_empty() {
                    let owner = extrachain_contract_sdk::ActorId::new(self.owner.clone())?;
                    return self.ledger.mint(context, owner, supply);
                }
                let mut distributed = 0_u128;
                for (actor, amount) in initial_balances {
                    distributed = distributed
                        .checked_add(amount.get())
                        .ok_or(extrachain_contract_sdk::ContractError::with_code(
                            extrachain_contract_sdk::ErrorCode::Overflow,
                            "Migration supply overflow",
                        ))?;
                    self.ledger.restore_balance(actor, amount)?;
                }
                if distributed != supply.get() {
                    return Err(extrachain_contract_sdk::ContractError::new(
                        "Migration supply does not match balances",
                    ));
                }
                context.emit("migrated", &distributed);
                Ok(extrachain_contract_sdk::OperationReceipt::new(
                    "migrated",
                    self.owner.as_str(),
                    distributed,
                ))
            }

            #[call]
            fn transfer(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                receiver: extrachain_contract_sdk::ActorId,
                amount: extrachain_contract_sdk::NonZeroAmount,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let sender = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.transfer(context, &sender, receiver, amount, &self.transfers)
            }

            #[call]
            fn approve(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                spender: extrachain_contract_sdk::ActorId,
                amount: u128,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let owner = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.approve(context, &owner, spender, amount)
            }

            #[call]
            fn transfer_from(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                owner: extrachain_contract_sdk::ActorId,
                receiver: extrachain_contract_sdk::ActorId,
                amount: extrachain_contract_sdk::NonZeroAmount,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let spender = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.transfer_from(
                    context,
                    &spender,
                    &owner,
                    receiver,
                    amount,
                    &self.transfers,
                )
            }

            #[call(access = "owner")]
            fn mint(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                receiver: extrachain_contract_sdk::ActorId,
                amount: extrachain_contract_sdk::NonZeroAmount,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                if !self.mint_enabled {
                    return Err(extrachain_contract_sdk::ContractError::new("Mint is disabled"));
                }
                self.ledger.mint(context, receiver, amount)
            }

            #[call(access = "owner")]
            fn revoke_mint(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
            ) -> extrachain_contract_sdk::ContractResult<()> {
                if !self.mint_enabled {
                    return Err(extrachain_contract_sdk::ContractError::new(
                        "Mint control is not available",
                    ));
                }
                self.mint_enabled = false;
                context.emit("mint_revoked", &());
                Ok(())
            }

            #[call]
            fn burn(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                amount: extrachain_contract_sdk::NonZeroAmount,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let owner = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.burn(context, &owner, amount)
            }

            #[query]
            fn balance_of(
                &self,
                owner: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<u128> {
                Ok(self.ledger.balance_of(&owner))
            }

            #[query]
            fn allowance(
                &self,
                owner: extrachain_contract_sdk::ActorId,
                spender: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<u128> {
                Ok(self.ledger.allowance(&owner, &spender))
            }

            #[query]
            fn locked_balance_of(
                &self,
                owner: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<u128> {
                Ok(self.ledger.locked_balance(&owner))
            }
        }
    }
    .into()
}

#[proc_macro_attribute]
pub fn nft_collection(attribute: TokenStream, input: TokenStream) -> TokenStream {
    let arguments =
        parse_macro_input!(attribute with Punctuated::<Meta, Token![,]>::parse_terminated);
    let mut collection_name = None;
    let mut collection_symbol = None;
    for argument in arguments {
        let Meta::NameValue(option) = argument else {
            return syn::Error::new_spanned(argument, "collection options use name = value")
                .into_compile_error()
                .into();
        };
        if !option.path.is_ident("name") && !option.path.is_ident("symbol") {
            return syn::Error::new_spanned(option, "unsupported NFT collection option")
                .into_compile_error()
                .into();
        }
        let syn::Expr::Lit(value) = &option.value else {
            return syn::Error::new_spanned(option, "name and symbol must be strings")
                .into_compile_error()
                .into();
        };
        let syn::Lit::Str(value) = &value.lit else {
            return syn::Error::new_spanned(value, "name and symbol must be strings")
                .into_compile_error()
                .into();
        };
        if option.path.is_ident("name") {
            collection_name = Some(value.clone());
        } else {
            collection_symbol = Some(value.clone());
        }
    }
    let Some(collection_name) = collection_name else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing collection name")
            .into_compile_error()
            .into();
    };
    let Some(collection_symbol) = collection_symbol else {
        return syn::Error::new(proc_macro2::Span::call_site(), "missing collection symbol")
            .into_compile_error()
            .into();
    };
    if collection_name.value().is_empty() || collection_name.value().len() > 64 {
        return syn::Error::new_spanned(
            collection_name,
            "collection name must contain 1 to 64 bytes",
        )
        .into_compile_error()
        .into();
    }
    if collection_symbol.value().is_empty() || collection_symbol.value().len() > 12 {
        return syn::Error::new_spanned(
            collection_symbol,
            "collection symbol must contain 1 to 12 bytes",
        )
        .into_compile_error()
        .into();
    }

    let item = parse_macro_input!(input as ItemStruct);
    let valid_shape = matches!(&item.fields, Fields::Unit)
        || matches!(&item.fields, Fields::Named(fields) if fields.named.is_empty());
    if !valid_shape {
        return syn::Error::new_spanned(
            item,
            "a standard NFT collection declaration cannot contain fields",
        )
        .into_compile_error()
        .into();
    }
    let visibility = &item.vis;
    let name = &item.ident;
    let metadata = quote::format_ident!("{}Metadata", name);

    quote! {
        #[derive(
            Clone,
            Debug,
            PartialEq,
            Eq,
            extrachain_contract_sdk::ContractCodec,
        )]
        struct #metadata {
            owner: extrachain_contract_sdk::ActorId,
            file: alloc::string::String,
            hash: alloc::string::String,
        }

        #[extrachain_contract_sdk::contract(version = 2, owner = "owner", upgrade = "owner")]
        #[derive(Clone, Debug, Default, PartialEq, Eq)]
        #visibility struct #name {
            owner: alloc::string::String,
            name: alloc::string::String,
            symbol: alloc::string::String,
            mint_enabled: bool,
            ledger: extrachain_contract_components::NftLedger,
            metadata: extrachain_contract_sdk::StateMap<u128, #metadata, 16_384>,
        }

        #[extrachain_contract_sdk::contract]
        impl #name {
            #[init]
            fn init(
                &mut self,
                context: &extrachain_contract_sdk::Context<'_>,
            ) -> extrachain_contract_sdk::ContractResult<()> {
                self.owner = alloc::string::ToString::to_string(context.caller());
                self.name = alloc::string::String::from(#collection_name);
                self.symbol = alloc::string::String::from(#collection_symbol);
                self.mint_enabled = true;
                Ok(())
            }

            #[call(access = "owner")]
            fn mint(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                token_id: u128,
                receiver: extrachain_contract_sdk::ActorId,
                metadata_owner: extrachain_contract_sdk::ActorId,
                metadata_file: extrachain_contract_sdk::BoundedString<256>,
                metadata_hash: extrachain_contract_sdk::BoundedString<64>,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                if !self.mint_enabled {
                    return Err(extrachain_contract_sdk::ContractError::new("Mint is disabled"));
                }
                if !context.has_dfs_proof(
                    metadata_owner.as_str(),
                    metadata_file.as_str(),
                    metadata_hash.as_str(),
                ) {
                    return Err(extrachain_contract_sdk::ContractError::with_code(
                        extrachain_contract_sdk::ErrorCode::VerificationFailed,
                        "NFT metadata is not verified",
                    ));
                }
                let receipt = self.ledger.mint(context, token_id, receiver)?;
                self.metadata.insert(
                    token_id,
                    #metadata {
                        owner: metadata_owner,
                        file: metadata_file.into_string(),
                        hash: metadata_hash.into_string(),
                    },
                )?;
                Ok(receipt)
            }

            #[call]
            fn approve(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                token_id: u128,
                actor: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let caller = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.approve(context, &caller, token_id, actor)
            }

            #[call]
            fn transfer(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                token_id: u128,
                receiver: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let caller = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.transfer(context, &caller, token_id, receiver)
            }

            #[call]
            fn transfer_from(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                token_id: u128,
                owner: extrachain_contract_sdk::ActorId,
                receiver: extrachain_contract_sdk::ActorId,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                if self.ledger.owner_of(token_id) != Some(&owner) {
                    return Err(extrachain_contract_sdk::ContractError::new(
                        "The expected owner does not own the token",
                    ));
                }
                let caller = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                self.ledger.transfer(context, &caller, token_id, receiver)
            }

            #[call]
            fn burn(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
                token_id: u128,
            ) -> extrachain_contract_sdk::ContractResult<extrachain_contract_sdk::OperationReceipt> {
                let caller = extrachain_contract_sdk::ActorId::new(
                    alloc::string::ToString::to_string(context.caller()),
                )?;
                let receipt = self.ledger.burn(context, &caller, token_id)?;
                self.metadata.remove(&token_id);
                Ok(receipt)
            }

            #[query]
            fn owner_of(
                &self,
                token_id: u128,
            ) -> extrachain_contract_sdk::ContractResult<alloc::string::String> {
                Ok(alloc::string::ToString::to_string(
                    self.ledger
                        .owner_of(token_id)
                        .ok_or(extrachain_contract_sdk::ContractError::with_code(
                            extrachain_contract_sdk::ErrorCode::NotFound,
                            "The token does not exist",
                        ))?
                        .as_str(),
                ))
            }

            #[query]
            fn metadata_of(
                &self,
                token_id: u128,
            ) -> extrachain_contract_sdk::ContractResult<(
                extrachain_contract_sdk::ActorId,
                alloc::string::String,
                alloc::string::String,
            )> {
                let metadata = self.metadata.get(&token_id).ok_or(
                    extrachain_contract_sdk::ContractError::with_code(
                        extrachain_contract_sdk::ErrorCode::NotFound,
                        "Metadata does not exist",
                    ),
                )?;
                Ok((
                    metadata.owner.clone(),
                    metadata.file.clone(),
                    metadata.hash.clone(),
                ))
            }

            #[call(access = "owner")]
            fn revoke_mint(
                &mut self,
                context: &mut extrachain_contract_sdk::Context<'_>,
            ) -> extrachain_contract_sdk::ContractResult<()> {
                if !self.mint_enabled {
                    return Err(extrachain_contract_sdk::ContractError::new(
                        "Mint control is not available",
                    ));
                }
                self.mint_enabled = false;
                context.emit("mint_revoked", &());
                Ok(())
            }
        }
    }
    .into()
}

#[proc_macro_attribute]
pub fn contract(attribute: TokenStream, input: TokenStream) -> TokenStream {
    if syn::parse::<ItemStruct>(input.clone()).is_ok() {
        return contract_struct(attribute, input);
    }
    if !attribute.is_empty() {
        return syn::Error::new(
            proc_macro2::Span::call_site(),
            "contract implementation does not accept options",
        )
        .into_compile_error()
        .into();
    }
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
        let init = match take_route_attribute(&mut method.attrs, "init") {
            Ok(value) => value,
            Err(error) => return error.into_compile_error().into(),
        };
        let call = match take_route_attribute(&mut method.attrs, "call") {
            Ok(value) => value,
            Err(error) => return error.into_compile_error().into(),
        };
        let query = match take_route_attribute(&mut method.attrs, "query") {
            Ok(value) => value,
            Err(error) => return error.into_compile_error().into(),
        };
        let authorize_upgrade = match take_route_attribute(&mut method.attrs, "authorize_upgrade") {
            Ok(value) => value,
            Err(error) => return error.into_compile_error().into(),
        };
        let migrate = match take_route_attribute(&mut method.attrs, "migrate") {
            Ok(value) => value,
            Err(error) => return error.into_compile_error().into(),
        };
        let owner_only = take_attribute(&mut method.attrs, "owner_only");
        let role_count = [init, call, query, authorize_upgrade, migrate]
            .into_iter()
            .filter(Option::is_some)
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
        if [init, call, query, authorize_upgrade]
            .into_iter()
            .flatten()
            .any(|options| options.from.is_some() || options.to.is_some())
        {
            return syn::Error::new_spanned(
                method,
                "only a migration can declare from and to versions",
            )
            .into_compile_error()
            .into();
        }
        if let Some(options) = migrate
            && (!matches!((options.from, options.to), (Some(from), Some(to)) if from < to))
        {
            return syn::Error::new_spanned(
                method,
                "a migration needs increasing from and to versions",
            )
            .into_compile_error()
            .into();
        }
        let method_name = if init.is_some() {
            "init".to_string()
        } else if authorize_upgrade.is_some() {
            "authorize_upgrade".to_string()
        } else if migrate.is_some() {
            "migrate".to_string()
        } else {
            method.sig.ident.to_string()
        };
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
        let state_changing = call.is_some() || init.is_some() || migrate.is_some();
        if state_changing && receiver.mutability.is_none() {
            return syn::Error::new_spanned(method, "state-changing methods require &mut self")
                .into_compile_error()
                .into();
        }
        if query.is_some() && receiver.mutability.is_some() {
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
        let access = init
            .or(call)
            .or(query)
            .or(authorize_upgrade)
            .or(migrate)
            .unwrap_or_default()
            .access;
        let owner_guard = (owner_only || access == Access::Owner).then(|| {
            quote! {
                ::extrachain_contract_sdk::ContractConfiguration::require_owner(
                    &__state,
                    __context.caller(),
                )?;
            }
        });
        let init_guard = init.is_some().then(|| {
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

    let automatic_upgrade = (!known_methods.contains("authorize_upgrade")).then(|| {
        quote! {
            "authorize_upgrade" => {
                let __result: ::extrachain_contract_sdk::ContractResult<_> = (|| {
                    let mut __decoder = ::extrachain_contract_sdk::Decoder::new(&request.arguments);
                    if __decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != 1 {
                        return Err(::extrachain_contract_sdk::ContractError::new("Invalid contract arguments"));
                    }
                    let _: ::extrachain_contract_sdk::BoundedString<64> =
                        ::extrachain_contract_sdk::ContractValue::decode_value(&mut __decoder)?;
                    if !__decoder.is_empty() {
                        return Err(::extrachain_contract_sdk::ContractError::new(
                            "Contract arguments have trailing data",
                        ));
                    }
                    ::extrachain_contract_sdk::ContractConfiguration::authorize_upgrade(
                        &__state,
                        request.caller.as_str(),
                    )?;
                    Ok(::extrachain_contract_sdk::InvokeResponse::success(
                        request.state.clone(),
                        ::extrachain_contract_sdk::encode_result(&()),
                        alloc::vec::Vec::new(),
                    ))
                })();
                match __result {
                    Ok(response) => response,
                    Err(error) => ::extrachain_contract_sdk::InvokeResponse::failure(request.state, error),
                }
            },
        }
    });
    let automatic_migration = (!known_methods.contains("migrate")).then(|| {
        quote! {
            "migrate" => {
                let __result: ::extrachain_contract_sdk::ContractResult<_> = (|| {
                    let mut __decoder = ::extrachain_contract_sdk::Decoder::new(&request.arguments);
                    if __decoder.array().map_err(::extrachain_contract_sdk::ContractError::codec)? != 0
                        || !__decoder.is_empty()
                    {
                        return Err(::extrachain_contract_sdk::ContractError::new("Invalid contract arguments"));
                    }
                    ::extrachain_contract_sdk::ContractConfiguration::require_owner(
                        &__state,
                        request.caller.as_str(),
                    )?;
                    Ok(::extrachain_contract_sdk::InvokeResponse::success(
                        ::extrachain_contract_sdk::ContractState::encode_state(&__state),
                        ::extrachain_contract_sdk::encode_result(&()),
                        alloc::vec::Vec::new(),
                    ))
                })();
                match __result {
                    Ok(response) => response,
                    Err(error) => ::extrachain_contract_sdk::InvokeResponse::failure(request.state, error),
                }
            },
        }
    });

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
                    #automatic_upgrade
                    #automatic_migration
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
