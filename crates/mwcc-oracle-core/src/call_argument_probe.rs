//! Guarded regular-call argument observer installation on the pinned retail images.
use crate::{
    coff::{Builder, Object, Relocation},
    executable::{self, Original},
};
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
};
pub const FORCE_HANDLER: &str = "_OracleRust_ArgumentForceGpr";
pub const FORCE_RETAIL: &str = "_OracleRust_ArgumentForceGprRetail";
#[derive(Clone, Debug, Serialize)]
pub struct Guard {
    pub version: &'static str,
    pub name: &'static str,
    pub address: u32,
    pub size: u32,
    pub code_sha256: &'static str,
    pub normalized_sha256: &'static str,
    pub relocation_identity_sha256: &'static str,
}
const GUARDS: &[Guard] = &[
    Guard {
        version: "GC_1_3",
        name: "_FunctionCalls_GenerateCall",
        address: 0x5267c0,
        size: 0x6a6,
        code_sha256: "78d7d1fa2ad0de9cd90373233c344f8764449874f9ab080ff60e795c0dbe376b",
        normalized_sha256: "0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc",
        relocation_identity_sha256: "48600848ce105c2d81525dfccfc1be01c038340c70a672dadd3eb425b70ad3bf",
    },
    Guard {
        version: "GC_1_3",
        name: "_evaluate_hascall_arguments_reverse",
        address: 0x526e70,
        size: 0x12b,
        code_sha256: "5511b9a8a99cdbb762e250251b35d9c5b8b989c74da43a55d59554f5bbab0a91",
        normalized_sha256: "f793edd798bad9583dec0c6b1fb5d43d2325926da0a5f608d4ae91a8690c5a7e",
        relocation_identity_sha256: "6c19d247bb972b613ad86416c85aad2c5bdb6cac4fde798e220b44179fba7ebf",
    },
    Guard {
        version: "GC_1_3",
        name: "_load_argument_registers",
        address: 0x526fa0,
        size: 0x6e7,
        code_sha256: "4e2c20bbb66259e33ad1b6d0ee6c4a931b01e253c16e97c3bd87ebd99170e67d",
        normalized_sha256: "6083bda177ba5768dadf7a16c3897ff7262d3e6abe0008f1859d86ceee9ec42a",
        relocation_identity_sha256: "7e39ebfe956b3ea019f0a7503ef41d50827d41c899f95b2daf637281300f5c8d",
    },
    Guard {
        version: "GC_1_3",
        name: "_store_argument_on_stack",
        address: 0x527690,
        size: 0x38d,
        code_sha256: "2df871b2abd3733f3a986348e741a0b8cf2d2a47952e749aed88c89d4de4ddff",
        normalized_sha256: "eeff069a9382b3dca5467dc136d44a0b27215de8eb8842934131bc15f8e5e583",
        relocation_identity_sha256: "73a5eff558a739288b997852ff30c48e9c4470d02df1b4a4474253283d7d84a3",
    },
    Guard {
        version: "GC_1_3",
        name: "_assign_argument_locations",
        address: 0x527a20,
        size: 0x60f,
        code_sha256: "2fed6f79fc410292b5191205b8bf8e70118d0c5c9f74d8411b3a59689328b4ab",
        normalized_sha256: "e32e15bd87d57e5880e5da845ea28ad026bc48b75cb559ba5619bc80dd5e287b",
        relocation_identity_sha256: "5b4afe8d5d1e2f92628cb0de06f959cece4b5deeca5327a1bcf7036ba304e524",
    },
    Guard {
        version: "GC_1_3",
        name: "_PCode_AppendInstruction",
        address: 0x4dba70,
        size: 0x41,
        code_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        normalized_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        relocation_identity_sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    Guard {
        version: "GC_1_3",
        name: "_generate_intrinsic_or_function_call",
        address: 0x44c8f0,
        size: 0x32,
        code_sha256: "2a6ebfd19bae02093b90db3738a7a33e733e832f613d6821ed3185fc61dced8f",
        normalized_sha256: "45f076ae1a6b12bd5ef9c3b1dad012eba0cdfd4603cf67d6bd8e8e0675d72a9c",
        relocation_identity_sha256: "6c8885294f5b9d5daea61e4931497c22d2e79447561410800d8ffc15f9543449",
    },
    Guard {
        version: "GC_1_3",
        name: "_Operands_ForceGPR",
        address: 0x4e1220,
        size: 0x42b,
        code_sha256: "61efe622d9e99eccc0132faaba2544bd71410251709a062f374319749aee6502",
        normalized_sha256: "52d4d8e36e24a35b43292ca4aa5e813e8066913707c099609ba97f4fd6309c83",
        relocation_identity_sha256: "d99e6b482aa486ddf911b8beeb6d30c23c720e9dbfa5aa7f7ba408fbcf6c1ae7",
    },
    Guard {
        version: "GC_1_3",
        name: "_Operands_Normalize",
        address: 0x4e1650,
        size: 0x229,
        code_sha256: "6b8ffbb25f16cb570f65d0f6ebc6dcc535ab704dbbdfd8e1b0648ab3d626c698",
        normalized_sha256: "cdd84dd317bc1ccc73d71c61534230d8f725df0f972e7cfabe42fa3e2ae63b1d",
        relocation_identity_sha256: "773738abc81311731d6cf016f833a37d56eaeb2c96b2c70cec9ec2e98a407b24",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_FunctionCalls_GenerateCall",
        address: 0x527320,
        size: 0x6a6,
        code_sha256: "15c67ac1fc2c6f71b65acf3534aedcd16b7a451be32a4782fc3bdd9affb7b465",
        normalized_sha256: "0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc",
        relocation_identity_sha256: "caf7484980b3e088a2963adc2ae11461a1250a5bcbe1317aa87fcf5e4fb56b8e",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_evaluate_hascall_arguments_reverse",
        address: 0x5279d0,
        size: 0x12b,
        code_sha256: "7200b3ac51d6634dd9efdf66cca82ebb53be3f13c9955163b4480e7efd730400",
        normalized_sha256: "f793edd798bad9583dec0c6b1fb5d43d2325926da0a5f608d4ae91a8690c5a7e",
        relocation_identity_sha256: "6c19d247bb972b613ad86416c85aad2c5bdb6cac4fde798e220b44179fba7ebf",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_load_argument_registers",
        address: 0x527b00,
        size: 0x6e7,
        code_sha256: "7e66deaa49bf3919e24e025d8ac1ea195645122eaf68af90e63396e92861cab6",
        normalized_sha256: "6083bda177ba5768dadf7a16c3897ff7262d3e6abe0008f1859d86ceee9ec42a",
        relocation_identity_sha256: "6c3d09107fadf948239847f99537c0366d5798c57289b3d075f70bcd09dd393d",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_store_argument_on_stack",
        address: 0x5281f0,
        size: 0x38d,
        code_sha256: "6aeb0b95b26e7806a91632595092f7677b77d8de203a305dc37af6daa9632633",
        normalized_sha256: "eeff069a9382b3dca5467dc136d44a0b27215de8eb8842934131bc15f8e5e583",
        relocation_identity_sha256: "e6cbc25f1b87cba19f7d1f3f3f715d350f6eb7d997b9ab640a19fd1d6aa9feb4",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_assign_argument_locations",
        address: 0x528580,
        size: 0x60f,
        code_sha256: "1d533de934a60abb7557cb3ed50d76841e8a10fe8a7e97e66534f9f8da1548c3",
        normalized_sha256: "e32e15bd87d57e5880e5da845ea28ad026bc48b75cb559ba5619bc80dd5e287b",
        relocation_identity_sha256: "b53723b69e71f1c4e279f39269a102443dd8cb4beff756bfb2ecfa73f50a997f",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_PCode_AppendInstruction",
        address: 0x4dc270,
        size: 0x41,
        code_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        normalized_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        relocation_identity_sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_generate_intrinsic_or_function_call",
        address: 0x44c7c0,
        size: 0x32,
        code_sha256: "29caa4c0f03d1400af51a29a046cd7e822122d0a862cc974cd3e77c5100de1c3",
        normalized_sha256: "45f076ae1a6b12bd5ef9c3b1dad012eba0cdfd4603cf67d6bd8e8e0675d72a9c",
        relocation_identity_sha256: "6c8885294f5b9d5daea61e4931497c22d2e79447561410800d8ffc15f9543449",
    },
    Guard {
        version: "GC_1_3_2",
        name: "_fn_004e10c0",
        address: 0x4e10c0,
        size: 0x1d24,
        code_sha256: "17290054cd1e916539ed034dac36a9a49603f4e04f01de304b0d36dd07040695",
        normalized_sha256: "c689b05c417c642886c10f34530dfd76e29e488d34f80098a814e0fa8e7d6187",
        relocation_identity_sha256: "59f06e84f4f9bdb95fd0a1ff3b4b1f0ff0d14cae38794292cc1d41c8ddbb5eca",
    },
    Guard {
        version: "GC_2_6",
        name: "_FunctionCalls_GenerateCall",
        address: 0x528290,
        size: 0x6a6,
        code_sha256: "5023832d8bfe474c81e024d84b4af5b4ff73668855d8d807ddb2de66d96f43dd",
        normalized_sha256: "0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc",
        relocation_identity_sha256: "dda3e990d024d2c5eee772d1122eca5d58b8daa2de9616320ec19072ae2636bb",
    },
    Guard {
        version: "GC_2_6",
        name: "_evaluate_hascall_arguments_reverse",
        address: 0x528940,
        size: 0x12b,
        code_sha256: "e83f5a7118a9241dec229db540bd35d263beff14670b14b778a6164a83700b14",
        normalized_sha256: "f793edd798bad9583dec0c6b1fb5d43d2325926da0a5f608d4ae91a8690c5a7e",
        relocation_identity_sha256: "6c19d247bb972b613ad86416c85aad2c5bdb6cac4fde798e220b44179fba7ebf",
    },
    Guard {
        version: "GC_2_6",
        name: "_load_argument_registers",
        address: 0x528a70,
        size: 0x6e7,
        code_sha256: "6d23fd451bfeb8ce63df3c52d32970d6fd069d817d5e80cfddf99e8c756c757c",
        normalized_sha256: "6083bda177ba5768dadf7a16c3897ff7262d3e6abe0008f1859d86ceee9ec42a",
        relocation_identity_sha256: "67faca01dc83c15d9aa5fce544d0fda9ac430b497ec5624c49494211ee9e8312",
    },
    Guard {
        version: "GC_2_6",
        name: "_store_argument_on_stack",
        address: 0x529160,
        size: 0x38d,
        code_sha256: "cd15b78d0677a8852b2826074b88e702444e73d9faeac9c79b77342fa3b7e8c5",
        normalized_sha256: "eeff069a9382b3dca5467dc136d44a0b27215de8eb8842934131bc15f8e5e583",
        relocation_identity_sha256: "8cccaf2d6d8635b6918446a7712f2b35f4a09c8ccf0255b0ece238607e2dacdb",
    },
    Guard {
        version: "GC_2_6",
        name: "_assign_argument_locations",
        address: 0x5294f0,
        size: 0x60f,
        code_sha256: "37cf44afd29850b25a946ac6c3e2dfdb114650acad93f0584130623982acd531",
        normalized_sha256: "e32e15bd87d57e5880e5da845ea28ad026bc48b75cb559ba5619bc80dd5e287b",
        relocation_identity_sha256: "37c8e82d778573c25f7165d9e7c2a8abcd7c6f28b3307a28d175ed5d16940a35",
    },
    Guard {
        version: "GC_2_6",
        name: "_PCode_AppendInstruction",
        address: 0x4dcff0,
        size: 0x41,
        code_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        normalized_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        relocation_identity_sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    Guard {
        version: "GC_2_6",
        name: "_generate_intrinsic_or_function_call",
        address: 0x44d130,
        size: 0x32,
        code_sha256: "06ed718a2a47b2a0807eb24a0ab7a9eff491bc994f2ddeccf26db1ff89dbb305",
        normalized_sha256: "45f076ae1a6b12bd5ef9c3b1dad012eba0cdfd4603cf67d6bd8e8e0675d72a9c",
        relocation_identity_sha256: "6c8885294f5b9d5daea61e4931497c22d2e79447561410800d8ffc15f9543449",
    },
    Guard {
        version: "GC_2_6",
        name: "_fn_004e1d90",
        address: 0x4e1f10,
        size: 0x1d24,
        code_sha256: "eb58d53018fa2b80c983118b95dc224e597020b37b3808cb922943c39e94b0e5",
        normalized_sha256: "c689b05c417c642886c10f34530dfd76e29e488d34f80098a814e0fa8e7d6187",
        relocation_identity_sha256: "7d474a0d82192d83a0f62977d448b98e7a1a572ec0840124fb9a707b2686f482",
    },
    Guard {
        version: "GC_2_7",
        name: "_FunctionCalls_GenerateCall",
        address: 0x5281b0,
        size: 0x6a6,
        code_sha256: "5a3c226ef2a155b5c2a051585e32ecfd663eff6889557f91db5c74a375c61156",
        normalized_sha256: "0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc",
        relocation_identity_sha256: "a7f3a7628fb520d27927a0aedd11fc43194fd7c29b8a665e5e268ee3d25d576a",
    },
    Guard {
        version: "GC_2_7",
        name: "_evaluate_hascall_arguments_reverse",
        address: 0x528860,
        size: 0x12b,
        code_sha256: "e83f5a7118a9241dec229db540bd35d263beff14670b14b778a6164a83700b14",
        normalized_sha256: "f793edd798bad9583dec0c6b1fb5d43d2325926da0a5f608d4ae91a8690c5a7e",
        relocation_identity_sha256: "6c19d247bb972b613ad86416c85aad2c5bdb6cac4fde798e220b44179fba7ebf",
    },
    Guard {
        version: "GC_2_7",
        name: "_load_argument_registers",
        address: 0x528990,
        size: 0x6e7,
        code_sha256: "b998b06af5d3aa21db03043eca67f2bc502d208a93f5543c4266405d0ba400c9",
        normalized_sha256: "6083bda177ba5768dadf7a16c3897ff7262d3e6abe0008f1859d86ceee9ec42a",
        relocation_identity_sha256: "79e51a568e61ff34468829b3fd22d7bd66ab18c1602251286a15bc8f41c698fb",
    },
    Guard {
        version: "GC_2_7",
        name: "_store_argument_on_stack",
        address: 0x529080,
        size: 0x38d,
        code_sha256: "99c4c47de07a646c8b61d6dbbe4ea250a363aa045bed0b2fecb995eb09a09b09",
        normalized_sha256: "eeff069a9382b3dca5467dc136d44a0b27215de8eb8842934131bc15f8e5e583",
        relocation_identity_sha256: "2d344eb8509e9e4b2d02d84e8ffcc694a172cea2f15ed801aa269733367e2f2a",
    },
    Guard {
        version: "GC_2_7",
        name: "_assign_argument_locations",
        address: 0x529410,
        size: 0x60f,
        code_sha256: "06bf382f4ceeac7b5ce86f147cf782daa1d69665d04e271865a81a0eb33a5388",
        normalized_sha256: "e32e15bd87d57e5880e5da845ea28ad026bc48b75cb559ba5619bc80dd5e287b",
        relocation_identity_sha256: "29262adf09de920663a9680a831be70ee005f7805aa8cf82aee190bf62dab6dd",
    },
    Guard {
        version: "GC_2_7",
        name: "_PCode_AppendInstruction",
        address: 0x4dce70,
        size: 0x41,
        code_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        normalized_sha256: "8c85cb195c6e5fc2434e3e42cc25f2a024bdf0655d373e8f3e1cb330f2b27c58",
        relocation_identity_sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    Guard {
        version: "GC_2_7",
        name: "_generate_intrinsic_or_function_call",
        address: 0x44d130,
        size: 0x32,
        code_sha256: "3a7a21b5dd5db39b171c1eaaa205ef399b00b7eac6303a766085d95fae01dc5f",
        normalized_sha256: "45f076ae1a6b12bd5ef9c3b1dad012eba0cdfd4603cf67d6bd8e8e0675d72a9c",
        relocation_identity_sha256: "6c8885294f5b9d5daea61e4931497c22d2e79447561410800d8ffc15f9543449",
    },
    Guard {
        version: "GC_2_7",
        name: "_fn_004e1d90",
        address: 0x4e1d90,
        size: 0x1d24,
        code_sha256: "f296381df19f4cd4e62dd9eac4b423a97da190d99feaf8d872bfb508815b1bac",
        normalized_sha256: "c689b05c417c642886c10f34530dfd76e29e488d34f80098a814e0fa8e7d6187",
        relocation_identity_sha256: "a7eb8885485a897c6d00687c9254472ba21d05068c959db91e54492fe0f860e1",
    },
];
#[derive(Debug)]
pub struct Installed {
    pub objects: BTreeMap<String, Vec<u8>>,
    pub islands: Vec<u8>,
    pub guards: Vec<Guard>,
}
fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
pub fn install(root: &Path, version: &str, inputs: &[(String, Vec<u8>)]) -> Result<Installed> {
    let guards = GUARDS
        .iter()
        .filter(|g| g.version == version)
        .cloned()
        .collect::<Vec<_>>();
    ensure!(!guards.is_empty(), "unsupported call argument version");
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    for g in &guards {
        let s = metadata
            .iter()
            .find(|s| s.name == g.name)
            .context("missing call argument helper metadata")?;
        ensure!(
            s.address == g.address && executable::size(&s.options)? == g.size,
            "call argument helper address/size guard failed for {}",
            g.name
        );
        ensure!(
            digest(pe.read(g.address, g.size as usize)?) == g.code_sha256,
            "call argument helper PE guard failed for {}",
            g.name
        );
    }
    let mut checked = BTreeSet::new();
    let mut objects = BTreeMap::new();
    let mut patched = false;
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for g in &guards {
            let Some(s) = object
                .symbols
                .values()
                .find(|s| s.section > 0 && s.name == g.name)
            else {
                continue;
            };
            ensure!(
                checked.insert(g.name),
                "duplicate call argument helper {}",
                g.name
            );
            let section = &object.sections[s.section as usize - 1];
            let mut actual = section
                .data
                .get(s.value as usize..(s.value + g.size) as usize)
                .context("call argument helper outside split section")?
                .to_vec();
            let mut expected = pe.read(g.address, g.size as usize)?.to_vec();
            let mut identities = Sha256::new();
            let mut relocs = section
                .relocations
                .iter()
                .filter(|r| (s.value..s.value + g.size).contains(&r.offset))
                .collect::<Vec<_>>();
            relocs.sort_by_key(|r| r.offset);
            for r in relocs {
                let at = (r.offset - s.value) as usize;
                ensure!(
                    matches!(r.kind, 6 | 20) && at + 4 <= actual.len(),
                    "invalid call argument helper relocation"
                );
                let target = object
                    .symbols
                    .get(&r.symbol)
                    .context("missing call argument helper relocation target")?;
                identities.update((at as u32).to_le_bytes());
                identities.update(r.kind.to_le_bytes());
                identities.update((target.name.len() as u32).to_le_bytes());
                identities.update(target.name.as_bytes());
                identities.update(target.value.to_le_bytes());
                identities.update(target.section.to_le_bytes());
                identities.update(target.kind.to_le_bytes());
                identities.update([target.storage]);
                identities.update((target.auxiliary.len() as u32).to_le_bytes());
                for auxiliary in &target.auxiliary {
                    identities.update(auxiliary)
                }
                identities.update(&actual[at..at + 4]);
                actual[at..at + 4].fill(0);
                expected[at..at + 4].fill(0);
            }
            ensure!(
                actual == expected && digest(&actual) == g.normalized_sha256,
                "call argument helper split byte guard failed for {}",
                g.name
            );
            ensure!(
                format!("{:x}", identities.finalize()) == g.relocation_identity_sha256,
                "call argument helper relocation identity guard failed for {}",
                g.name
            );
        }
        let Some(helper) = object
            .symbols
            .values()
            .find(|s| s.section > 0 && s.name == "_load_argument_registers")
        else {
            continue;
        };
        ensure!(!patched, "duplicate argument register helper");
        patched = true;
        let section_index = helper.section as usize - 1;
        let offset = helper.value + 0x2ed;
        let section = &object.sections[section_index];
        ensure!(
            section.data.get(offset as usize) == Some(&0xe8),
            "argument force call opcode changed"
        );
        let r = section
            .relocations
            .iter()
            .find(|r| r.offset == offset + 1 && r.kind == 20)
            .context("argument force call lacks relocation")?;
        let target = &object.symbols[&r.symbol];
        let (retail, addend) = if version == "GC_1_3" {
            ("_Operands_ForceGPR", 0u32)
        } else {
            (
                if version == "GC_1_3_2" {
                    "_fn_004e10c0"
                } else {
                    "_fn_004e1d90"
                },
                0xd00,
            )
        };
        ensure!(
            target.name == retail
                && u32::from_le_bytes(
                    section.data[offset as usize + 1..offset as usize + 5].try_into()?
                ) == addend,
            "argument force call target changed"
        );
        let mut rebuilt = Builder::default();
        for s in &object.sections {
            rebuilt.section(&s.name, s.flags, s.data.to_vec(), s.size, vec![])?;
        }
        let mut symbols = BTreeMap::new();
        for (&index, s) in &object.symbols {
            let section_symbol = s.storage == 3
                && s.value == 0
                && s.section > 0
                && s.auxiliary.len() == 1
                && s.name == object.sections[s.section as usize - 1].name;
            let new = if section_symbol {
                rebuilt.section_symbols[&s.section]
            } else {
                ensure!(
                    s.auxiliary.is_empty(),
                    "unsupported argument input auxiliary symbol"
                );
                rebuilt.add_symbol(&s.name, s.value, s.section, s.kind, s.storage, &[])?
            };
            symbols.insert(index, new);
        }
        for (index, s) in object.sections.iter().enumerate() {
            rebuilt.sections[index].relocations = s
                .relocations
                .iter()
                .map(|r| Relocation {
                    offset: r.offset,
                    symbol: symbols[&r.symbol],
                    kind: r.kind,
                })
                .collect();
        }
        let handler = rebuilt.external(FORCE_HANDLER)?;
        let s = &mut rebuilt.sections[section_index];
        s.data[offset as usize + 1..offset as usize + 5].fill(0);
        s.relocations
            .iter_mut()
            .find(|r| r.offset == offset + 1)
            .unwrap()
            .symbol = handler;
        rebuilt.refresh_section_definitions()?;
        objects.insert(path.clone(), rebuilt.finish()?);
    }
    ensure!(
        checked.len() == guards.len() && patched,
        "missing guarded call argument split helpers"
    );
    let (retail, addend) = if version == "GC_1_3" {
        ("_Operands_ForceGPR", 0u32)
    } else {
        (
            if version == "GC_1_3_2" {
                "_fn_004e10c0"
            } else {
                "_fn_004e1d90"
            },
            0xd00,
        )
    };
    let mut island = Builder::default();
    let target = island.external(retail)?;
    let mut code = vec![0xe9];
    code.extend(addend.to_le_bytes());
    let section = island.section(
        ".text",
        0x60500020,
        code,
        5,
        vec![Relocation {
            offset: 1,
            symbol: target,
            kind: 20,
        }],
    )?;
    island.add_symbol(FORCE_RETAIL, 0, section, 0x20, 2, &[])?;
    island.refresh_section_definitions()?;
    Ok(Installed {
        objects,
        islands: island.finish()?,
        guards,
    })
}
