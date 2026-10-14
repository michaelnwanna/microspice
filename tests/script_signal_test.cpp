#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) return 1;
    const std::string fixture = argv[2];
    {
        std::ofstream output(fixture);
        output << "voltage_source(\"V1\", supply, ground, 10V)\n";
        for (std::size_t index = 0; index < 240; ++index) {
            const auto suffix = std::to_string(index);
            output << "resistor(\"RA" << suffix << "\", supply, node_" << suffix
                   << ", 1kohm)\n"
                   << "resistor(\"RB" << suffix << "\", node_" << suffix
                   << ", ground, 1kohm)\n"
                   << "capacitor(\"C" << suffix << "\", node_" << suffix
                   << ", ground, 1uF)\n";
        }
        output << "voltage_source(\"ready\", lonely, ground, 0V)\n"
                  "transient(\"charging\", step=100us, stop=5ms)\n";
        if (!output) return 1;
    }
    int descriptors[2];
    if (pipe(descriptors) != 0) return 1;
    const auto child = fork();
    if (child == -1) return 1;
    if (child == 0) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDERR_FILENO) == -1 ||
            dup2(descriptors[1], STDOUT_FILENO) == -1)
            _exit(127);
        close(descriptors[1]);
        execl(argv[1], argv[1], "run", fixture.c_str(),
              static_cast<char*>(nullptr));
        _exit(127);
    }
    close(descriptors[1]);
    std::string captured;
    bool signalled = false;
    char character;
    while (true) {
        const auto count = read(descriptors[0], &character, 1);
        if (count == -1 && errno == EINTR) continue;
        if (count <= 0) break;
        captured += character;
        if (!signalled && character == '\n') {
            signalled = true;
            kill(child, SIGINT);
        }
    }
    close(descriptors[0]);
    int exitCode = 0;
    while (waitpid(child, &exitCode, 0) == -1)
        if (errno != EINTR) return 1;
    std::remove(fixture.c_str());
    if (!signalled || !WIFEXITED(exitCode) || WEXITSTATUS(exitCode) != 130 ||
        captured.find("warning:") == std::string::npos ||
        captured.find("Cancelled") == std::string::npos ||
        captured.find("Analysis charging") != std::string::npos) {
        std::cerr << "CLI signal cancellation failed: " << captured << '\n';
        return 1;
    }
    std::cout << "CLI SIGINT returned 130 without publishing results\n";
}