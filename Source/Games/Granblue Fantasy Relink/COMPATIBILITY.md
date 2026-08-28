# Granblue Fantasy: Relink compatibility

## Fork release latest-6

- Bumps the GBFR addon version to 2.
- Supports game 2.0.4 and 2.0.5 through runtime executable-version selection.
- Rejects unknown versions and known versions whose native hook/reset contracts do not match.
- Installs the three native hooks transactionally and synchronizes device-state publication across device destruction/recreation.
- Retains the fork's native SDR/HDR selection and GBFR-specific resource handling.

## Verified executables

| Game | SHA-256 | Size | PE timestamp |
| --- | --- | ---: | --- |
| 2.0.4 | `f827f3c13caa90b290fab2fe7e28165a80448fde0a3f7a96d79dac6b8343ff2a` | 123,512,288 | 2026-08-03 10:23:33 +08:00 |
| 2.0.5 | `7189b958ff0fe5238cea28a2939ffdad6e3a9acb14dd274a9fcc8e7e275bd175` | 123,517,408 | 2026-08-13 18:54:38 +08:00 |

## Native address evidence

Rizin disassembly and exact-byte searches against both PE images established these RVAs:

| Purpose | 2.0.4 | 2.0.5 | Evidence |
| --- | ---: | ---: | --- |
| Initialize DX11 rendering pipeline | `0x007F4420` | `0x007F4760` | Exact 55-byte function prologue/body match; RIP-relative operands differ |
| TAA component init | `0x021607B0` | `0x021608B0` | Same prologue and references to `filtershadertaa.vso`, `ps_filtertaa.pso`, `ps_filter_taa2.pso`, `ps_filterscaledtaa.pso`, and `ps_filtertup.pso` |
| TAA transition/jitter hook | `0x02160960` | `0x02160A60` | Same function structure; 2.0.5 directly references all TAA globals below |
| Render width/height | `0x06B822D8` / `0x06B822DC` | unchanged | 2.0.5 transition reads width at `0x2160D6B`; adjacent height global retained |
| Camera index | `0x0701F560` | `0x0701F780` | 2.0.5 transition reads it at `0x2160A86`, bounds-checks against 11, then indexes camera table |
| Camera table | `0x054BC3A0` | unchanged | 2.0.5 transition loads `table[index * 8]` at `0x2160A92` |
| TAA settings inline buffer | `0x0703DD10` | `0x0703DF30` | Same `+0x220` data shift as camera index and related globals |
| TAA running flag pointer | `0x073725B8` | `0x07372848` | 2.0.5 transition loads the qword then compares byte at target at `0x2160AA1` |
| Render-scale settings pointer | `0x07031030` | `0x07031250` | 2.0.5 transition loads the qword then tests byte `+0x65` at `0x2160D71` |
| Jitter phase counter | `0x0703D6B0` | `0x0703D8D0` | 2.0.5 transition loads it at `0x2160E4D`, masks with `0x3F`, and indexes `[component + phase*8 + 0x28]` |
| TAA reset flag | `0x07372290` | `0x07372520` | Paired `cmp [rip+disp32],1` / `mov [rip+disp32],0` instructions at transition offsets `+0x119/+0x122` resolve directly to the configured flag in both builds |

The shifts are deliberately not treated as a universal rule: code moved by `+0x100` or `+0x340`, some data stayed fixed, and data groups moved by `+0x220` or `+0x290`. Hook functions and actively consumed TAA globals were derived from matching code and checked in the 2.0.5 consumer. Runtime preflight also decodes both reset-flag RIP targets before installing any hook.

## Shader hooks and packaging

All GBFR replacement and detection shader hashes were inspected. They identify D3D11 shader bytecode, not executable locations. The 2.0.5 TAA component still initializes the same named shader programs and preserves the component/jitter-table layout, so no hash change is supported by the executable evidence. Runtime shader observation in game remains the final validation for non-TAA post-process permutations.
