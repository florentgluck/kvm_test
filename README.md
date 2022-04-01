# README

Dummy KVM code that triggers two VMexits:

- PMIO write
- hlt instruction

The goal of this dummy example is to illustrate an issue encountered with Archlinux when this code is compilied with `-O3`.
In this scenario, no PMIO VMexit is triggered.

To run the test:
```
make run
```

If everything goes well, you should get the following outpout:
```
VMexit: PMIO port=0x123 value=42
VMexit: KVM_EXIT_HLT
```
