# compy - Universal Application Launcher

Compy is a versatile application launcher designed to run binaries from various ecosystems—native Host (RPM/DNF), isolated Debian containers (`chroot`), AppImage, Flatpak, and a dedicated Windows Subsystem (QEMU/Virtio-sock)—all through a single command-line interface.

## 🚀 1. Installation

The installation process is automated by the `compy-install.sh` script, which handles dependency installation, setting up the Debian environment, compiling the main `compy` binary, and configuring the Windows Subsystem daemon (`hostd`).

### 1.1 Prerequisites

Before running the installation script, ensure you have:

* **A Fedora/RHEL-based Host System** (The script uses `dnf`).
* **Git** installed.
* **The Windows 10 ISO file** (`win10.iso`). **NOTE:** This must be legally acquired by the user and copied to the target directory manually.

### 1.2 Installation Steps

1.  **Clone the Repository:**
    ```bash
    git clone [https://github.com/zavorateam-libs/compy-linux.git](https://github.com/zavorateam-libs/compy-linux.git) /usr/local/src/compy_project
    cd /usr/local/src/compy_project/compy
    ```

2.  **Copy the Windows ISO (Manual Step):**
    Place your Windows 10 ISO file into designated dir:
    ```bash
    sudo mkdir -p /opt/compy/winsys
    # Replace /path/to/your/Windows.iso with the actual path
    sudo cp /path/to/your/Windows.iso /opt/compy/winsys/win10.iso
    ```

3.  **Run the Installer:**
    The installer handles all required setup, including installing dependencies (`debootstrap`, `qemu-kvm`), compiling `compy-proxy`, `hostd`, and the main `compy` binary.
    ```bash
    sudo bash compy-install.sh 
    ```
    *Note: The script is assumed to be `compy-install.sh` located in the `compy/` directory.*

## ⚙️ 2. Configuration and Setup

### 2.1 Debian Environment (`:deb:`)

The installation script automatically sets up the Debian `chroot` environment at `/opt/compy/debbin` and compiles the static proxy loader (`/opt/compy/debbin/bin/compy-proxy`).

**Verification:** You should confirm that your system has access to network resources inside the container.
```bash
sudo compy :deb:/usr/bin/apt update
```

### 2.2 Windows Subsystem (:win:)

This layer requires a running QEMU Virtual Machine managed by the hostd daemon.

#### 2.2.1 Daemon Startup

The installer automatically starts the hostd daemon. If it crashes or the system reboots, you must restart it manually:
```bash
sudo /opt/compy/winsys/hostd/hostd &
```

The daemon starts QEMU in the background and listens for commands on the socket /tmp/compy_hostd.sock.

#### 2.2.2 Windows Installation (Manual)

After initial setup, you must manually install Windows 10 onto the created virtual disk image (/opt/compy/winsys/win10.qcow2). This typically involves connecting to the running QEMU instance via VNC or serial console (which the daemon runs without a display).

#### 2.2.3 Windows Agent (agent)

For compy to execute commands, you must install the corresponding agent application inside the Windows VM. This agent must listen to the virtio-serial socket (/tmp/win_comm.sock) and execute the commands sent by hostd.

Flow: compy → (UNIX Socket) → hostd → (Virtio-sock) → agent (inside Windows VM)

## 🛠️ 3. Usage

compy uses prefixes to route the command to the correct execution layer.
### Syntax
```bash
compy <PREFIX>:<application_path> [arguments...]
```

|Layer|Prefix|Example|Description|
|-----|------|-------|-----------|
|Host |(None)|compy /usr/bin/gedit|Runs native binaries via execvp.|
|Debian|:deb:|sudo compy :deb:/usr/bin/xterm|Runs binaries within the isolated /opt/compy/debbin environment.|
|Windows|:win:|compy :win:C:\MyApp\App.exe|Sends command to the running hostd daemon for execution inside the QEMU VM.|
|Flatpak|:flat:|compy :flat:org.gimp.GIMP|Executes the application using the host's flatpak run command.|
|AppImage|:app:|compy :app:~/Gimp.AppImage|Executes the AppImage directly via execvp.|

## ⚠️ 4. Troubleshooting and Recovery
### 4.1 System Instability or Mounting Errors

**Symptom:** The system becomes slow, fails to mount new directories, or shows the error: Compy: Mount error (bind): /sys: No space left on device.
**Cause:** This occurs when old chroot sessions crash without unmounting, filling the kernel's mount table (inode limit). 
**Solution** (Preferred): Reboot the host system.
```bash
sudo reboot
```
**Solution** (Manual Unmount): If a reboot is not possible, try a lazy, recursive unmount:
```bash
sudo umount -Rl /opt/compy/debbin
```
### 4.2 Debian Application Fails to Launch Silently

**Symptom:** The command sudo compy :deb:/usr/bin/xterm prints the starting message but immediately returns without launching the application.
**Cause:**
-   The static proxy loader (compy-proxy) could not be found or executed.
-   The target application (xterm) requires a library or device node that was not bind-mounted correctly.
**Solution:** Verify the proxy path: /opt/compy/debbin/bin/compy-proxy exists and is executable. Ensure that the C++ code for launch_debbin includes the critical Mount Namespace (unshare(CLONE_NEWNS)) setup.

### 4.3 Windows Execution Failure

**Symptom:** Command compy :win:C:\App.exe fails with Compy: ERROR: HOSTD Daemon not running. Start up hostd.
**Cause:** The hostd daemon is not running or the UNIX socket (/tmp/compy_hostd.sock) does not exist. 
**Solution:** Restart the daemon:
```bash
sudo /opt/compy/winsys/hostd/hostd &
```

**Symptom:** HostD daemon runs, but the application does not start in Windows. Cause:

-   QEMU may have crashed or failed to start (run_qemu.sh failed).

-   The agent is not installed or running inside the Windows VM, preventing communication over the virtio-serial socket.

**Solution:** Check the QEMU logs (defined in run_qemu.sh, usually /tmp/win_q.txt) and ensure the Windows VM is running and the agent application is operational.
