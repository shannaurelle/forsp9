# forsp9
A port of forsp from C to Plan 9 C
## Resources 

### Forsp
https://github.com/xorvoid/forsp - Forsp language source code and examples

https://xorvoid.com/forsp.html - Forsp language web article

### Plan 9 C
https://doc.cat-v.org/plan_9/programming/c_programming_in_plan_9 - Programming in Plan 9 C

https://plan9.io/sys/doc/comp.html - How to use the Plan 9 C Compiler

https://plan9.io/sys/doc/compiler.html - Plan 9 C compilers

### 9front
https://9front.org/iso/ - iso files

http://fqa.9front.org/ - Frequently Questioned Answers in 9front

## Tests

### QEMU Command
```
qemu-system-x86_64 -m 2048 -cpu Haswell -accel whpx,kernel-irqchip=off -smp 4,threads=4 -net nic,model=virtio,macaddr=52:54:00:12:34:56 -net user,hostfwd=tcp::5640-:564,hostfwd=tcp::17010-:17010,hostfwd=tcp::17019-:17019,hostfwd=tcp::12567-:567,hostfwd=tcp::17020-:17020 -device virtio-scsi-pci,id=scsi -drive if=none,id=vd0,file=9front.qcow2.img -device scsi-hd,drive=vd0 -audiodev sdl,id=audiodev-0,timer-period=2000 -device ich9-intel-hda -device hda-duplex,audiodev=audiodev-0,mixer=false,use-timer=true
```
### Relevant QEMU specifications
- QEMU version: 8.1.0
- Virtual hardware specifications: 2 GB RAM, Intel Haswell CPU (4 CPUs), 4 threads
- hardware accelerator : Windows Hypervisor Platform
- filename of QEMU 9cow2 image file: 9front.qcow2
- 9front version date of release: April 27, 2024

### Hardware specifications
- Architecture: x86 64-bit
- OS: Windows 11 Pro 64-bit (10.0, Build 22000)
- CPU: Intel Core i5-4300U @ 1.9 GHz (4 CPUs), ~2.5 GHz
- RAM: 16 GB 
- System Model: HP Elitebook 840 G1

### Test Cases
These programs are found in the examples folder at [xorvoid's github repository of forsp](https://github.com/xorvoid/forsp)
- currying.fp
- demo.fp
- factorial.fp
- fibonnaci-functional.fp
- fibonnaci-imperative.fp
- forsp.fp
- higher-order-functions.fp
- low-level.fp
- tutorial.fp
