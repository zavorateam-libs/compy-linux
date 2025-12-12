#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <pwd.h>
#include <errno.h>
#include <sched.h>
#include <fcntl.h>

const std::string DEB_ROOT = "/opt/compy/debbin";
const std::string HOST_SOCKET = "/tmp/compy_hostd.sock";

// Вспомогательные функции (предполагаем, что они определены)
void bind_mount(const std::string& source, const std::string& target) {
    if (mount(source.c_str(), target.c_str(), NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror(("Compy: Mount error (bind): " + source).c_str());
    }
}

void launch_debbin(const std::string& app_path, const std::vector<std::string>& args) {
    std::cout << "Compy: Starting DEB-app in isolated environment: " << app_path << std::endl;

    if (unshare(CLONE_NEWNS) == -1) {
	perror("Compy: Error: Failed to unshare mount namespace (CRITICAL)");
	exit(EXIT_FAILURE);
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE,  NULL) == -1) {
	perror("Compy: Error: Failed to make root private (CRITICAL)");
	exit(EXIT_FAILURE);
    }
    // --- UTILITIES FOR APT/DPKG CHECK ---
    std::string app_filename = app_path.substr(app_path.find_last_of('/') + 1);
    bool is_net_app = (app_filename == "apt" || app_filename == "apt-get" || app_filename == "dpkg");

    // 0. Ensure base directories exist
    if (access((DEB_ROOT + "/dev").c_str(), F_OK) == -1 && mkdir((DEB_ROOT + "/dev").c_str(), 0755) != 0) { 
        perror("Compy: Unable to create base /dev directory"); exit(EXIT_FAILURE);
    }
    if (access((DEB_ROOT + "/dev/pts").c_str(), F_OK) == -1 && mkdir((DEB_ROOT + "/dev/pts").c_str(), 0755) != 0) { 
        perror("Compy: Unable to create /dev/pts"); exit(EXIT_FAILURE);
    }
    if (access((DEB_ROOT + "/dev/shm").c_str(), F_OK) == -1 && mkdir((DEB_ROOT + "/dev/shm").c_str(), 0777) != 0) { 
        perror("Compy: Unable to create /dev/shm"); exit(EXIT_FAILURE);
    }

    // --- MOUNTS BEFORE CHROOT ---

    // Bind mounts for system APIs
    if (mount("/proc", (DEB_ROOT + "/proc").c_str(), NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror("Compy: mount error. Error mounting proc");
    }
    bind_mount("/sys", DEB_ROOT + "/sys");

    // CRITICAL FIX #1: Bind-mount the host /dev (Simpler than tmpfs)
    bind_mount("/dev", DEB_ROOT + "/dev"); 
    
    // CRITICAL FIX #2: Mount /dev/shm and /dev/pts OVER the bind-mount
    // This is safer than just using the host's /dev/shm and /dev/pts.
    if (mount("tmpfs", (DEB_ROOT + "/dev/shm").c_str(), "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") != 0) {
        perror("Compy: Mount error (tmpfs /dev/shm)");
    }
    
    if (mount("devpts", (DEB_ROOT + "/dev/pts").c_str(), "devpts", 0, "newinstance,ptmxmode=0666,mode=620") != 0) {
        perror("Compy: Mount error (devpts pre-chroot CRITICAL)");
    }
    
    // Conditional Network/DNS mount for apt/dpkg
    if (is_net_app) {
        bind_mount("/etc/resolv.conf", DEB_ROOT + "/etc/resolv.conf");
    }

    // Standard Bind mounts for Graphics/Home
    bind_mount("/tmp", DEB_ROOT + "/tmp");
    bind_mount("/run/user", DEB_ROOT + "/run/user");

    // Bind mount for HOME directory and .Xauthority
    std::string user_home = getenv("HOME");
    std::string target_home = DEB_ROOT + user_home;
    if (access(target_home.c_str(), F_OK) == -1 && mkdir(target_home.c_str(), 0755) != 0) {
        perror(("Compy: Warning: Could not create target home dir: " + target_home).c_str());
    }
    bind_mount(user_home, target_home);
    
    std::string xauth_path = user_home + "/.Xauthority";
    std::string target_xauth_path = DEB_ROOT + user_home + "/.Xauthority";
    if (access(xauth_path.c_str(), F_OK) == 0) {
        bind_mount(xauth_path, target_xauth_path);
    }

    // 3. Change Root Directory
    if (chroot(DEB_ROOT.c_str()) != 0) {
        perror("Compy: chroot error. Check that debbin dir exists.");
        exit(EXIT_FAILURE);
    }
    chdir("/"); 

    // --- DEVICE NODES (Mknod is only needed for systems that fail to bind-mount /dev) ---
    // В этом паттерне mknod обычно не требуется, так как мы используем bind-mount /dev.
    // Оставляем его только для критически важных узлов, если bind-mount их не предоставляет корректно.

    if (mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3)) != 0 && errno != EEXIST) { perror("Compy: mknod /dev/null failed"); }
    // /dev/tty и /dev/console должны быть предоставлены bind-mount'ом.
    
    // 4. Environment Cleanup
    const char* user_name = getenv("SUDO_USER");
    if (user_name) {
        struct passwd *pw = getpwnam(user_name);
        if (pw) {
            setenv("HOME", pw->pw_dir, 1);
            setenv("USER", user_name, 1);
            setenv("LOGNAME", user_name, 1);
        }
    }

    // Unset conflicting variables
    unsetenv("LD_PRELOAD");
    // ... (other unsetenv calls)
    unsetenv("WAYLAND_DISPLAY"); 

    // Forward necessary display variables (X11 only)
    setenv("DISPLAY", getenv("DISPLAY"), 1);

    // 5. Clean up File Descriptors 
    close(STDIN_FILENO);

    // 6. Launch Application using the Static Proxy Loader
    const char* PROXY_PATH = "/bin/compy-proxy"; 
    
    std::vector<char*> proxy_exec_args;
    proxy_exec_args.push_back(const_cast<char*>(PROXY_PATH));
    proxy_exec_args.push_back(const_cast<char*>(app_path.c_str())); 

    for (size_t i = 1; i < args.size(); ++i) {
        proxy_exec_args.push_back(const_cast<char*>(args[i].c_str()));
    }
    proxy_exec_args.push_back(nullptr); 

    if (execvp(PROXY_PATH, proxy_exec_args.data()) == -1) {
        perror(("Compy: Final execvp error: Proxy not found or failed to start: " + std::string(PROXY_PATH)).c_str());
        exit(EXIT_FAILURE);
    }
}

// ----------------------------------------------------------------------
// WINDOWS FUNCTION
// ----------------------------------------------------------------------

void launch_windows_app(const std::string& exe_path) {
    std::cout << "Compy: Transer command for Windows-daemon: " << exe_path << std::endl;

    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un server_addr;

    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, HOST_SOCKET.c_str(), sizeof(server_addr.sun_path) - 1);
    server_addr.sun_path[sizeof(server_addr.sun_path) - 1] = '\0';

    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "Compy: ERROR: HOSTD Daemon not running. Start up hostd." << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string command = "RUN " + exe_path;
    send(client_fd, command.c_str(), command.length(), 0);

    close(client_fd);
    std::cout << "Compy: Command send." << std::endl;
}

// ----------------------------------------------------------------------
// MAIN ROUTER FUNCTION
// ----------------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Use: compy <bin/prefix> [args...]" << std::endl;
        std::cerr << "Ex: 	compy /usr/bin/gedit (HOST)" << std::endl;
        std::cerr << "		compy :deb:/usr/bin/vlc (DEB)" << std::endl;
        std::cerr << "		compy :win:C:\\App.exe (Windows)" << std::endl;
        std::cerr << "		compy :app:~/Gimp.AppImage (AppImage)" << std::endl;
        std::cerr << "		compy :flat:org.gimp.GIMP (Flatpak)" << std::endl;
        return 1;
    }

    std::string arg1 = argv[1];
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    if (arg1.rfind(":deb:", 0) == 0) {
        // --- DEB Layer ---
        std::string deb_path = arg1.substr(5);
        // Change prefix to absolute path for execvp
        args[0] = deb_path;
        launch_debbin(deb_path, args);

    } else if (arg1.rfind(":win:", 0) == 0) {
        // --- WINDOWS Layer ---
        launch_windows_app(arg1.substr(5));

    } else if (arg1.rfind(":flat:", 0) == 0) {
        // --- FLATPAK Layer ---
        std::string app_id = arg1.substr(6);
        std::cout << "Compy: Starting Flatpak: " << app_id << std::endl;
        std::string cmd = "flatpak run " + app_id;
        for (size_t i = 2; i < args.size(); ++i) {
            cmd += " " + args[i]; // Additional args
        }
        if (execlp("/usr/bin/flatpak", "flatpak", "run", app_id.c_str(), (char*)NULL) == -1) {
             perror("Compy: execvp error: Flatpak or application is not found");
             return 1;
        }

    } else if (arg1.rfind(":app:", 0) == 0) {
        // --- APPIMAGE Layer  ---
        std::string app_path = arg1.substr(5);
        std::cout << "Compy: Starting AppImage: " << app_path << std::endl;

        args[0] = app_path;
        std::vector<char*> exec_args;
        for (const auto& arg : args) {
            exec_args.push_back(const_cast<char*>(arg.c_str()));
        }
        exec_args.push_back(nullptr);

        if (execvp(app_path.c_str(), exec_args.data()) == -1) {
             perror(("Compy: execvp error: AppImage application is not found" + app_path).c_str());
             return 1;
        }
    } else {
        // --- HOST Layer (Fedora RPM) ---
        std::cout << "Compy: Starting on host: " << arg1 << std::endl;
        std::vector<char*> exec_args;
        for (const auto& arg : args) {
            exec_args.push_back(const_cast<char*>(arg.c_str()));
        }
        exec_args.push_back(nullptr);

        if (execvp(arg1.c_str(), exec_args.data()) == -1) {
             perror(("Compy: execvp error: Binary not found" + arg1).c_str());
             return 1;
        }
    }

    return 0;
}
