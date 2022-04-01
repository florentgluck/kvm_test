# README

Tiny KVM code that triggers two VMexits:

- PMIO write
- `hlt` instruction

The goal of this code is to illustrate an issue encountered on some Archlinux systems when compiled with `-O3`.
In this scenario, no PMIO VMexit was triggered.

To run the test:
```
make run
```

If everything goes well, you should get the following output:
```
VMexit: PMIO port=0x123 value=42
VMexit: KVM_EXIT_HLT
```
