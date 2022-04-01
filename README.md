# README

Dummy KVM code that triggers two VMexits:
- PMIO write
- hlt instruction

The goal of this dummy example is to illustrate an issue encountered with Archlinux when this code is compilied with -O3.
In this scenario, no PMIO VMexit is triggered.
