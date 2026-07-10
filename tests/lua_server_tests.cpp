#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
  if (argc < 7) {
    std::cerr << "server test requires Demi, project, DTLS certificate, key, CA, and server name.\n";
    return 2;
  }

  const std::filesystem::path demi = argv[1];
  const std::filesystem::path project = argv[2];
  (void)setenv("DEMI_DTLS_CERT", argv[3], 1);
  (void)setenv("DEMI_DTLS_KEY", argv[4], 1);
  (void)setenv("DEMI_DTLS_CA", argv[5], 1);
  (void)setenv("DEMI_DTLS_SERVER_NAME", argv[6], 1);
  const std::filesystem::path clientProject = project.parent_path() / "tests/client/demi.project.json";
  const std::filesystem::path serverLog =
    std::filesystem::temp_directory_path() / "minimal_2d_android_server_test.log";
  const std::filesystem::path clientLog =
    std::filesystem::temp_directory_path() / "minimal_2d_android_client_test.log";
  const std::string command = demi.string() + " serve --project " +
    project.string() + " > " + serverLog.string() + " 2>&1 & echo $!";
  FILE* pipe = popen(command.c_str(), "r");
  int pid = 0;
  if (pipe != nullptr) {
    (void)fscanf(pipe, "%d", &pid);
    pclose(pipe);
  }
  if (pid <= 0) {
    std::cerr << "failed to start server.\n";
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const std::string clientCommand = demi.string() + " serve --project " +
    clientProject.string() + " > " + clientLog.string() + " 2>&1";
  const int clientResult = std::system(clientCommand.c_str());
  (void)std::system(("kill " + std::to_string(pid)).c_str());

  std::ifstream clientOutput(clientLog);
  const std::string output{
    std::istreambuf_iterator<char>(clientOutput),
    std::istreambuf_iterator<char>(),
  };
  if (clientResult != 0 ||
      output.find("LUA_SERVER_HANDSHAKE_OK") == std::string::npos ||
      output.find("LUA_SERVER_CLAIM_OK") == std::string::npos) {
    std::cerr << "Lua NetworkSession did not complete the server handshake and claim flow.\n" << output;
    return 1;
  }
  return 0;
}
