cmd_/home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.ko := ld -r -m elf_x86_64  -z max-page-size=0x200000  --build-id  -T ./scripts/module-common.lds -o /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.ko /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.o /home/dom/LDD3-Notes/Code_Examples/Hello_World/hello.mod.o;  true
