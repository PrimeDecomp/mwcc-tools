use mwcc_oracle_core::{
    coff::{self, Builder, Object, Relocation, SymbolEdit},
    corpus,
};

const CODE: u32 = 0x60000020;
const FUNCTION: u16 = 0x20;

/// _f (static) calls itself and _g; _g calls _f; a DIR32 to _data+4.
fn object() -> Vec<u8> {
    let mut b = Builder::default();
    let mut code = vec![0xe8, 0, 0, 0, 0, 0xc3, 0xe8, 0, 0, 0, 0, 0xc3];
    code.extend([0xe8, 0, 0, 0, 0, 0xa1, 4, 0, 0, 0, 0xc3, 0x90]);
    let text = b.section(".text", CODE, code, 24, vec![]).unwrap();
    let f = b.add_symbol("_f", 0, text, FUNCTION, 3, &[]).unwrap();
    let g = b
        .add_symbol("_long_function_name", 12, text, FUNCTION, 2, &[])
        .unwrap();
    let data = b.external("_data").unwrap();
    b.sections[0].relocations = vec![
        Relocation {
            offset: 1,
            symbol: f,
            kind: 20,
        },
        Relocation {
            offset: 7,
            symbol: g,
            kind: 20,
        },
        Relocation {
            offset: 13,
            symbol: f,
            kind: 20,
        },
        Relocation {
            offset: 18,
            symbol: data,
            kind: 6,
        },
    ];
    b.refresh_section_definitions().unwrap();
    b.finish().unwrap()
}

#[test]
fn symbol_edits_rename_add_retarget_and_offset() {
    let data = object();
    let before = Object::parse(&data).unwrap();
    let f = *before
        .symbols
        .iter()
        .find(|(_, s)| s.name == "_f")
        .unwrap()
        .0;
    let external = *before
        .symbols
        .iter()
        .find(|(_, s)| s.name == "_data")
        .unwrap()
        .0;
    let mut edit = SymbolEdit::default();
    edit.renames.insert(f, "_hook__retail".into());
    edit.storage.insert(f, 2);
    edit.renames.insert(external, "_copts".into());
    edit.added.push(("_hook".into(), 0, 0, FUNCTION, 2));
    edit.added
        .push(("_an_alias_longer_than_eight".into(), 12, 1, FUNCTION, 2));
    // (the call from _g, outside _f's body)
    edit.retarget.insert((1, 2), before.symbol_count);
    edit.addends.push((1, 18, 0xf8));
    let after = coff::edit_symbols(&data, &edit).unwrap();
    let object = Object::parse(&after).unwrap();
    let name = |index: u32| object.symbols[&index].name.clone();
    assert_eq!(name(f), "_hook__retail");
    assert_eq!(object.symbols[&f].storage, 2);
    assert_eq!(name(external), "_copts");
    assert_eq!(object.symbol_count, before.symbol_count + 2);
    assert_eq!(name(before.symbol_count), "_hook");
    assert_eq!(name(before.symbol_count + 1), "_an_alias_longer_than_eight");
    assert_eq!(object.symbols[&(before.symbol_count + 1)].value, 12);
    let relocations = &object.sections[0].relocations;
    assert_eq!(relocations[0].symbol, f);
    assert_eq!(relocations[2].symbol, before.symbol_count);
    assert_eq!(&object.sections[0].data[18..22], &[0xfc, 0, 0, 0]);
    assert!(
        coff::defined_symbols(&after)
            .unwrap()
            .contains("_hook__retail")
    );
    assert!(
        coff::defined_symbols(&after)
            .unwrap()
            .contains("_long_function_name")
    );
}

#[test]
fn traces_compare_without_addresses_or_provenance() {
    let ours = "ce.object name=@6 caller=52ce78\n\
        pv.scalar.gap schema=1\n\
        st.block p=009f1000 index=7 weight=4520784 flags=2004 succ=009f2000,\n\
        ra.i p=009f1010 op=5 flags=00000002 n=2 line=2 ops=0400382c9f00000000000000 030200000000182495000000\n\
        ra.end";
    let theirs = "ce.object name=@6 caller=4fe0a1\n\
        pv.function.begin schema=1 site=43348d\n\
        st.block p=00a01000 index=7 weight=5262816 flags=2004 succ=00a02000,\n\
        ra.i p=00a01010 op=5 flags=00000002 n=2 line=2 ops=04009066b200000000000000 030200000000b820a7000000\n\
        ra.end";
    assert_eq!(corpus::trace_difference(ours, theirs), None);
    let changed = theirs.replace("index=7", "index=8");
    assert!(corpus::trace_difference(ours, &changed).is_some());
    let constant = "ra.i ops=000400000000050000000000";
    assert_eq!(
        corpus::comparable_record(constant).as_deref(),
        Some(constant)
    );
    assert_eq!(
        corpus::comparable_record("x text=1048576 line=12").as_deref(),
        Some("x text=1048576 line=12")
    );
}
