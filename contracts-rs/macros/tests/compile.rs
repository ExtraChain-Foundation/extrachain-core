#[test]
fn contract_attributes_validate_public_signatures() {
    let cases = trybuild::TestCases::new();
    cases.pass("tests/ui/pass_contract.rs");
    cases.compile_fail("tests/ui/fail_mutable_query.rs");
    cases.compile_fail("tests/ui/fail_duplicate_route.rs");
    cases.compile_fail("tests/ui/fail_owner_guard_without_owner.rs");
    cases.compile_fail("tests/ui/fail_float_argument.rs");
}
