# Changelog

## 1.0.0 (2026-01-31)


### Features

* Allow uppercase letters in interface names ([d377218](https://github.com/varlink/libvarlink/commit/d377218338b706ce3ff517a80fc0f1b1cfd8fe80))
* compile with CC=clang ([2ce64b3](https://github.com/varlink/libvarlink/commit/2ce64b3c6ccef85afc3c26f1ce74c4cbea70ef16))


### Bug Fixes

* add bootstrap-sha to prevent version reset ([981d14f](https://github.com/varlink/libvarlink/commit/981d14febc1c0e6206ab7ef66076f7f777fe15f4))
* avoid use-after-free after varlink_call_unref() ([c4590a7](https://github.com/varlink/libvarlink/commit/c4590a7292eff208bb137fd4525d4895f881683d))
* compile with musl ([bd52e9e](https://github.com/varlink/libvarlink/commit/bd52e9e4790e44aeaa5336bb70aeec52079d43a6))
* correct issues pointed out by static code analysis ([a97a895](https://github.com/varlink/libvarlink/commit/a97a8954657ec1637e2271bc6e15e7b32a656402))
* correct issues pointed out by static code analysis ([e8db4dc](https://github.com/varlink/libvarlink/commit/e8db4dc9e82b4496cebf8e517f9098110ea020d4))
* correct issues pointed out by static code analysis ([09c94f0](https://github.com/varlink/libvarlink/commit/09c94f0842cf429af03d0acd714d83c0c7343460))
* correct issues pointed out by static code analysis ([8b54827](https://github.com/varlink/libvarlink/commit/8b548270e2855b80e36cccddb1d3686fcafed14b))
* correct issues pointed out by static code analysis ([f02c188](https://github.com/varlink/libvarlink/commit/f02c1887d030a39b15da15c8aa82804d83a96f6b))
* correct issues pointed out by static code analysis ([45d18ec](https://github.com/varlink/libvarlink/commit/45d18ec2c054c77d6f91959b106ded73613ad3b4))
* correct issues pointed out by static code analysis ([b4a440f](https://github.com/varlink/libvarlink/commit/b4a440fd0e3cfd3056617c8fa076c4d917833f25))
* correct main signature of lib/test-object.c ([3b17da2](https://github.com/varlink/libvarlink/commit/3b17da24fe149b389a4b8110d8660b14361b3212))
* correct the float number parsing for some locales ([eae2b52](https://github.com/varlink/libvarlink/commit/eae2b5238c87775363f353170602c97ea05b41b0))
* correct typo in error message of test-symbols ([1494abe](https://github.com/varlink/libvarlink/commit/1494abe33a48a1c192555834f007f492dbeeca17))
* correctly #include headers in type.h ([3068ecf](https://github.com/varlink/libvarlink/commit/3068ecf68d66b942d1ced18318fa6f7dfea0d36a))
* disable lint message ([e1a74a3](https://github.com/varlink/libvarlink/commit/e1a74a322451e526a3118d55ccb5550bbace7842))
* fail for ending with UTF8 surrogate ([eaef540](https://github.com/varlink/libvarlink/commit/eaef540f5d7d47cbf738d23531ead9d7dace2069))
* free stream-&gt;in if allocating stream-&gt;out fails ([0c5b495](https://github.com/varlink/libvarlink/commit/0c5b49584893e29af3e3a460064b00c965633cc8))
* improve the error handling ([54ad8f7](https://github.com/varlink/libvarlink/commit/54ad8f79b7e5fabac76ea4a3b4e2bd3b409dd4f0))
* limit the array/object depth to 1000 ([c52ed49](https://github.com/varlink/libvarlink/commit/c52ed49540ba0b28260dcfa5819eca42550e745e))
* mark unreachable code ([2c971d0](https://github.com/varlink/libvarlink/commit/2c971d0db282a1df80d4c9b52521454f2f54a03d))
* mark unused function parameters with UNUSED() ([25e4fda](https://github.com/varlink/libvarlink/commit/25e4fdac3a45819cefc2a17faeab5836c5fedbde))
* pay attention to ERANGE from strtod_l() ([9906559](https://github.com/varlink/libvarlink/commit/9906559ebd7439c0814de2833b3ec0fca5806951))
* prevent memory leak in object_add_field() ([451e816](https://github.com/varlink/libvarlink/commit/451e81638b0794d89677fa8c4752d7b6e232b1f5)), closes [#51](https://github.com/varlink/libvarlink/issues/51)
* recognize ERANGE error from strtol() ([7b1f630](https://github.com/varlink/libvarlink/commit/7b1f630ac2427f621e5c00986a63f43fdd2dfdd0))
* reformat with astyle ([ebbfe6b](https://github.com/varlink/libvarlink/commit/ebbfe6b88c21c66719313226f5a62ee1b43151f0))
* remove unneeded includes ([5dfc434](https://github.com/varlink/libvarlink/commit/5dfc434c730ac22f8f7cf63f6381a77256cb86b1))
* remove unused includes ([649c1f7](https://github.com/varlink/libvarlink/commit/649c1f73a8145e8b10ab3b8d456a29118386598b))
* remove unused local variables ([967e51e](https://github.com/varlink/libvarlink/commit/967e51eceee2153140f3a85b65f0e4589f4a4a79))
* return VARLINK_ERROR_PANIC on float Inf or NaN ([84e6638](https://github.com/varlink/libvarlink/commit/84e663802226267c491b60eeff069caf002baf9d))
* uri: "varlink help" not working with camel-case interface names ([58f0ab1](https://github.com/varlink/libvarlink/commit/58f0ab1a2625e8dc0f218cd766711773058e7870))
* use readelf with `--lto-syms` in case of LTO ([b966742](https://github.com/varlink/libvarlink/commit/b9667423cf312c6edc52fd0d249bf5cf626151f0))
* use sizeof buf instead of hardcoding read size ([5c4689c](https://github.com/varlink/libvarlink/commit/5c4689c29bf1110c4a63ae42cad0ec68112c6c15))
* use strtod_l ([f60ddf3](https://github.com/varlink/libvarlink/commit/f60ddf37ee14b1bac5e8682495decb897036055f))
* **varlink:** check the correct field for VARLINK_URI_PROTOCOL_NONE ([daade5b](https://github.com/varlink/libvarlink/commit/daade5bbe1ae098218967ea6431705d867ddc5ab))
* VarlinkStream not dispatching out data when write returns EAGAIN ([a1a34ce](https://github.com/varlink/libvarlink/commit/a1a34cee0351cda5e62cf167d13aaa449cb6a5ec))
* verify for correct utf-8 strings ([434e51d](https://github.com/varlink/libvarlink/commit/434e51d9a765281390b69c80c029b64ed61df909))
