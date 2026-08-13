function(extrachain_generate_embedded_contracts output_root contracts_root)
  set(generated_directory "${output_root}/contracts")
  set(generated_header "${generated_directory}/embedded_contracts.h")
  file(MAKE_DIRECTORY "${generated_directory}")
  file(WRITE "${generated_header}" "#pragma once\nnamespace ExtraChain::Contracts::Embedded {\n")

  find_program(EXTRACHAIN_XXD_EXECUTABLE xxd)
  foreach(contract_name IN
          ITEMS fungible_token_rust fungible_token_assemblyscript
                non_fungible_token_rust non_fungible_token_assemblyscript)
    set(contract_file "${contracts_root}/${contract_name}.wasm")
    if(EXTRACHAIN_XXD_EXECUTABLE)
      set(contract_fragment "${generated_directory}/${contract_name}.inc")
      execute_process(
        COMMAND ${EXTRACHAIN_XXD_EXECUTABLE} -i -n ${contract_name} "${contract_file}"
        OUTPUT_FILE "${contract_fragment}"
        COMMAND_ERROR_IS_FATAL ANY)
      file(APPEND "${generated_header}" "#include \"contracts/${contract_name}.inc\"\n")
    else()
      file(READ "${contract_file}" contract_hex HEX)
      string(REGEX REPLACE "(..)" "0x\\1," contract_bytes "${contract_hex}")
      file(APPEND "${generated_header}"
           "inline constexpr unsigned char ${contract_name}[] = {${contract_bytes}};\n")
    endif()
  endforeach()
  file(APPEND "${generated_header}" "}\n")
endfunction()
