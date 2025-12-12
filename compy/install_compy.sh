#!/bin/bash
# install_compy_repo_v3.sh: Complete installer for Compy, adapted to zavorateam-libs/compy-linux structure.
# Execute with sudo.

# --- 1. Settings and Variables ---
DEB_ROOT="/opt/compy/debbin"
WINSYS_ROOT="/opt/compy/winsys"
HOSTD_DIR="$WINSYS_ROOT/hostd"
QEMU_IMAGE="$WINSYS_ROOT/win10.qcow2"
WINDOWS_ISO="$WINSYS_ROOT/win10.iso"
PROJECT_DIR="/usr/local/src/compy_project"
# Adjusted to match the assumed GitHub structure
REPO_URL="https://github.com/zavorateam-libs/compy-linux.git" 
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "amd64") 
USER_NAME=${SUDO_USER:-$USER}

echo "--- Starting Compy Universal Installation (Repo Structure: zavorateam-libs/compy-linux) ---"

# --- 2. Host System Dependency Installation ---
echo "2. Installing host system dependencies (debootstrap, gcc, git, flatpak, QEMU/KVM)..."
sudo dnf install -y debootstrap gcc git make glibc-static flatpak qemu-kvm qemu-img || { 
    echo "Error: Failed to install key dependencies. Exiting."
    exit 1
}

# --- 3. Repository Cloning and Preparation ---
echo "3. Cloning repository from $REPO_URL..."
sudo mkdir -p $PROJECT_DIR
sudo chown -R $USER_NAME:$USER_NAME $(dirname $PROJECT_DIR) # Set permissions on parent for cloning
git clone $REPO_URL $PROJECT_DIR || {
    echo "Error: Failed to clone repository $REPO_URL. Exiting."
    exit 1
}
echo "Repository cloned successfully to $PROJECT_DIR"


# --- 4. Debian Environment Setup (DEB Layer) ---
echo "4. Setting up DEB environment in $DEB_ROOT..."
sudo mkdir -p $DEB_ROOT
sudo chown root:root $DEB_ROOT

if ! sudo debootstrap --arch=$ARCH stable $DEB_ROOT http://deb.debian.org/debian
then
    echo "Error: debootstrap failed to create base image. Exiting."
    exit 1
fi

# Create critical mount points
USER_HOME=$(eval echo ~${SUDO_USER:-$USER})
TARGET_HOME="$DEB_ROOT$USER_HOME"
sudo mkdir -p $DEB_ROOT/{proc,sys,tmp,etc}
sudo mkdir -p $DEB_ROOT/run/user
sudo mkdir -p $DEB_ROOT/dev/{pts,shm}
sudo mkdir -p $TARGET_HOME
sudo chown $USER_NAME:$USER_NAME $TARGET_HOME
sudo cp /etc/resolv.conf $DEB_ROOT/etc/resolv.conf


# --- 5. Build and Install Static Proxy Loader (compy-proxy) ---
echo "5. Building and installing static proxy compy-proxy using source: compy/compy-proxy.cpp..."
PROXY_SRC="$PROJECT_DIR/compy/compy-proxy.cpp"
PROXY_BIN="$DEB_ROOT/bin/compy-proxy"
sudo mkdir -p $DEB_ROOT/bin

if [ ! -f "$PROXY_SRC" ]; then
    echo "Error: compy-proxy.cpp not found in the repository. Check structure."
    exit 1
fi

# Compile proxy statically using the source code from the repo
sudo g++ -o $PROXY_BIN $PROXY_SRC -static -s || {
    echo "Error compiling compy-proxy. Exiting."
    exit 1
}
echo "Static Proxy installed successfully: $PROXY_BIN"


# --- 6. Build and Install Main Compy Binary ---
echo "6. Building and installing main Compy binary using source: compy/compy.cpp..."
COMPY_SRC="$PROJECT_DIR/compy/compy.cpp"

if [ ! -f "$COMPY_SRC" ]; then
    echo "Error: compy.cpp not found in the repository. Check structure."
    exit 1
}

pushd $PROJECT_DIR
# Compile the main compy binary
g++ -o compy $COMPY_SRC -std=c++17 -Wall -lstdc++ || {
    echo "Error: Failed to build compy binary."
    popd
    exit 1
}
popd

# Move final binary to PATH
sudo mv $PROJECT_DIR/compy /usr/local/bin/compy
sudo chmod +x /usr/local/bin/compy
echo "Main Compy binary installed to /usr/local/bin/compy"


# --- 7. Windows Subsystem Setup (QEMU/HOSTD) ---
echo "7. Setting up Windows Subsystem (QEMU/HOSTD)..."
sudo mkdir -p $HOSTD_DIR
sudo chown -R $USER_NAME:$USER_NAME $WINSYS_ROOT

# a) QEMU Image Creation and ISO Check
if [ ! -f "$WINDOWS_ISO" ]; then
    echo "!!! WARNING: Windows ISO is required. Please copy win10.iso to $WINSYS_ROOT."
fi

if [ ! -f "$QEMU_IMAGE" ]; then
    echo "Creating QEMU image ($QEMU_IMAGE)..."
    qemu-img create -f qcow2 $QEMU_IMAGE 60G || { 
        echo "Error creating QEMU image. Exiting."
        exit 1
    }
fi

# b) Copy and setup QEMU run script (run_q.sh)
echo "Setting up run_q.sh script..."
RUN_Q_SRC="$PROJECT_DIR/winhost/run_q.sh"
RUN_Q_DEST="$HOSTD_DIR/run_qemu.sh" # Renamed to match the system() call in hostd.cpp

if [ ! -f "$RUN_Q_SRC" ]; then
    echo "Error: run_q.sh not found in the repository. Check structure."
    exit 1
fi
sudo cp $RUN_Q_SRC $RUN_Q_DEST
sudo chmod +x $RUN_Q_DEST
echo "QEMU launch script setup complete."


# c) Compile HostD Daemon
echo "Compiling HostD daemon..."
HOSTD_SRC="$PROJECT_DIR/winhost/hostd.cpp"

if [ ! -f "$HOSTD_SRC" ]; then
    echo "Error: hostd.cpp not found in the repository. Check structure."
    exit 1
fi

g++ -o $HOSTD_DIR/hostd $HOSTD_SRC -std=c++17 -Wall || {
    echo "Error compiling HostD. Exiting."
    exit 1
}
echo "HostD compiled successfully."

# --- 8. Finalization and HostD Startup ---
echo "8. Starting HostD daemon..."
sudo $HOSTD_DIR/hostd & 
HOSTD_PID=$!
echo "HOSTD Daemon launched with PID: $HOSTD_PID"

echo "--- Compy Installation Complete! ---"
echo "--- Summary ---"
echo "DEB Layer: Ready to run (e.g., sudo compy :deb:/usr/bin/xterm)"
echo "WIN Layer: HostD daemon running. Windows VM needs initial setup."
echo "FLATPAK/APPIMAGE Layers: Ready to run."

# --- 9. System Recovery Instructions ---
echo -e "\n--- System Recovery Instructions ---"
echo "1. To stop the HostD daemon:"
echo "   sudo kill $HOSTD_PID"
echo "2. To recover from a mounting error ('No space left on device'):"
echo "   sudo reboot"
echo "3. Manual unmounting (if reboot is not an option):"
echo "   sudo umount -Rl $DEB_ROOT"
