//! Helpers shared by the integration tests (each test crate uses some of them).
#![allow(dead_code)]
use std::{
    fs,
    path::{Path, PathBuf},
};

/// tests/fixtures/SUB of the repository.
pub fn fixtures(sub: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures")
        .join(sub)
}

/// Every `.obj` under ROOT, with its path, into OBJECTS.
pub fn collect(root: &Path, objects: &mut Vec<(String, Vec<u8>)>) {
    for entry in fs::read_dir(root).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            collect(&path, objects);
        } else if path.extension().is_some_and(|s| s == "obj") {
            objects.push((path.to_string_lossy().into(), fs::read(path).unwrap()));
        }
    }
}

/// A synthetic complete-observation trace of `pipeline`: two values (`flag` and `temp`), one
/// coalescing pass, one graph.
pub fn register(number: u16, effect: u16) -> String {
    let effect = effect.to_le_bytes();
    let number = number.to_le_bytes();
    format!(
        "0004{:02x}{:02x}{:02x}{:02x}000000000000",
        effect[0], effect[1], number[0], number[1]
    )
}
pub fn instructions(prefix: &str, physical: bool) -> String {
    let (flag, temporary) = if physical { (5, 6) } else { (32, 33) };
    format!(
        "{prefix} p=aaa op=137 flags=80004000 n=2 line=10 ops={} 020000000000000000000000\n\
         {prefix} p=bbb op=139 flags=80000010 n=3 line=10 ops={} {} 060000000000000000000000\n\
         {prefix} p=ccc op=137 flags=80004000 n=2 line=20 ops={} 020001000000000000000000\n",
        register(temporary, 2),
        register(flag, 2),
        register(temporary, 1),
        register(flag, 2)
    )
}
pub fn trace() -> String {
    let mut text = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\n".to_owned();
    for (id, register, name) in [(1, 32, "666c6167"), (2, 33, "74656d70")] {
        text += &format!(
            "pv.value.birth schema=1 function=1 value={id} cls=4 register={register} count={} physical=32 site=400028 expression=0 phase=0 line=10 category=unknown\npv.value.object function=1 value={id} object={id:08x} info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex={name}\n",
            register + 1
        );
    }
    text += "pv.category.window function=1 births=2 f2=32 f3=32 f4=34 l2=32 l3=32 l4=34\nra.before function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n";
    text += &instructions("ra.i", false);
    text += "ra.end\npv.coalesce.begin schema=1 pass=1 cls=4 count=34 physical=32 first=34 last=34 special=1 function_id=1 creation=direct_counter_writes link=pipeline__Fii\npv.coalesce.candidate pass=1 candidate=1 instruction=bbb flags=80000010 opcode=139 operands=3 destination_kind=0 destination_class=4 destination=32 source_kind=0 source_class=4 source=33\n";
    for (condition, site, taken, eflags) in [
        ("copy_flag", "10f", 0, "40"),
        ("destination_kind", "17b", 0, "40"),
        ("register_class", "18b", 0, "40"),
        ("roots_differ", "1d6", 1, "0"),
        ("interference", "244", 0, "40"),
        ("destination_below_window", "271", 1, "80"),
    ] {
        text += &format!(
            "pv.coalesce.check pass=1 candidate=1 site={site} condition={condition} taken={taken} eflags={eflags} eax=0 ebx=21 ecx=0 edx=20 esi=0 edi=0 ebp=0\n"
        );
    }
    text += "pv.coalesce.end pass=1 candidates=1\npv.graph.begin schema=1 function=1 graph=1 births=2 cls=4 physical=32 start=32 count=34 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first=34 last=34\n";
    text += &instructions("pv.graph.instruction", false);
    text += "pv.graph.node v=32 visible=1 deg=1 flags=0 color=-1 n=33,\npv.graph.node v=33 visible=1 deg=1 flags=0 color=-1 n=32,\npv.graph.end function=1 graph=1 nodes=2 instructions=3 complete=1\nra.graph cls=4 k=29 start=32 count=34\nra.node v=32 p=1000 deg=1 flags=0 color=-1 n=33,\nra.node v=33 p=1020 deg=1 flags=0 color=-1 n=32,\nra.order cls=4 v=1000:0,1020:0,\nra.select cls=4 mask=0 after=60 ok=1 v=1000:5:0,1020:6:0,\nra.after function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n";
    text += &instructions("ra.i", true);
    text += "ra.end\npv.function.end function=1 births=2 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=34\n";
    text
}
