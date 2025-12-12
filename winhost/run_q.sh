#!/bin/bash

LOG_FILE="/tmp/win_q.txt"
IMAGE="/opt/compy/winsys/win10.qcow"
SOCKET="/tmp/win_comm.sock"

rm -f $SOCKET
qemu-system-x86_64 -enable-kvm -cpu host,hv_relaxed,hv_spinlocks=0x1fff,hv_time -smp 4 -m 4G -daemonize -name win-subsystem -display none -serial file:$LOG_FILE -device virtio-serial-pci -chardev socket,path=$SOCKET,server,nowait,id=vchannel -device virtserialport,chardev=vchannel,name=com.compy.agent -drive file=$IMAGE,if=virtio,format=qcow2 -vga virtio

echo "QEMU started as background process. Socket: $SOCKET"
