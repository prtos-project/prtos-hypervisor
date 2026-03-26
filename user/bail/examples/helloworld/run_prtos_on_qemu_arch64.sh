make clean
make resident_sw
bash run_build.sh

mkdir -p u-boot
cp ../../bin/u-boot.bin ./u-boot/
CMD=" \
qemu-system-aarch64 \
-machine virt,gic_version=3 \
-machine virtualization=true \
-cpu cortex-a72 \
-machine type=virt \
-m 4096 \
-smp 4 \
-bios ./u-boot/u-boot.bin \
-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on \
-nographic -no-reboot \
-chardev socket,id=qemu-monitor,host=localhost,port=8889,server=on,wait=off,telnet=on \
-mon qemu-monitor,mode=readline"

if [ $# -ne 0 ];then
    echo "prtos debug mode ..."
    $CMD -gdb tcp::1234  -S
else
    echo "prtos run mode ..."
    $CMD
fi
