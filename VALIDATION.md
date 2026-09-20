# LR0 Validation

## CI gate

The temporary GitHub builder must:

1. configure/build the host harness with `-Wall -Wextra -Wpedantic -Werror`;
2. run `codynex_lr0_host` and require `pass=true`;
3. require all 12 malformed candidates rejected;
4. require state-preserving behavior replacement;
5. require overflow rollback;
6. run `codynex_lr0_recovery` and require `pass=true`;
7. build the Android debug APK;
8. verify `libcodynex_lr0.so` exists for `arm64-v8a`;
9. verify `libcodynex_lr0.so` exists for `armeabi-v7a`;
10. verify ELF64/AArch64 and ELF32/ARM architecture metadata;
11. archive APK/native libraries/host JSON/SHA-256 evidence.

## Device gate

Start from cleared LR0 app data.

### Program A

1. open app;
2. load Program A template;
3. compile to external `candidate.cxe`;
4. activate;
5. call function 0 three times;
6. verify state 0 is 3.

### Live replacement

1. load Program B template;
2. compile to the same external candidate path;
3. activate;
4. verify state remains 3;
5. call function 0;
6. verify state becomes 8;
7. APK rebuild/reinstall count since initial install remains zero.

### Invalid update survival

1. corrupt candidate integrity;
2. attempt activation;
3. require rejection;
4. require active program still valid;
5. require state remains 8;
6. call function 0;
7. require state becomes 13.

### Cold restart

1. use the process-kill button;
2. reopen the app;
3. require recovery succeeds;
4. require state 0 remains 13;
5. call function 0;
6. require state becomes 18.

### Anti-cheat

Fail if:

- native runtime embeds Program A/B recipes;
- lab bypasses external file loading;
- candidate mutates active state before validation;
- state survives only through stale native objects;
- restart serializes interpreter stack/execution scratch;
- behavior edit requires another APK build;
- ARM32 packaging is missing.
