#include <iostream>
#include <fstream>  
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <sstream>

const char ERROR[30] = "An error has occurred\n";
const char* BUILTIN_PATH = "path";
const char* BUILTIN_CD = "cd";
const char* PROMPT = "wish> ";
const std::string EXIT = "exit";
const char* REDIR = ">";
const char* PARALLEL = "&";
const std::string PATH = "/bin";

struct Command {
    std::vector<char*> tokens;
    std::string redirFile = "";
    bool hasRedirection = false;
};

void error() {
    write(STDERR_FILENO, ERROR, strlen(ERROR));
}

void freeMem(std::vector<char*>& vec) {
    for (char*& t : vec) { 
        if (t) {
            delete[] t;
            t = nullptr; 
        }
    }
    vec.clear();
}

void redir(const std::string& file) {
    int fd = open(file.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        error();
        exit(1);
    }
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        error();
        close(fd);
        exit(1);
    }
    close(fd);
}

void child(Command& commands, const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        std::string exePath = path + "/" + commands.tokens.at(0);
        if (access(exePath.c_str(), X_OK) == 0) {
            if (commands.hasRedirection) redir(commands.redirFile);
            execv(exePath.c_str(), commands.tokens.data());
            error();
            exit(1);
        }
    }
    error();
    exit(1);
}

std::vector<std::string> set_paths(const std::vector<char*> &tokens) {
    std::vector<std::string> paths;
    for (size_t i = 1; i < tokens.size(); ++i) {
                paths.push_back(tokens.at(i));
    }
    return paths;
}

void parseRedir(Command& command) {
    int tokensEnd = command.tokens.size();
    for (auto i = 0; i < tokensEnd; i++) {
        if (strcmp(command.tokens[i], REDIR) == 0) {
            if (i == 0 || i != tokensEnd - 2) {
                error();
                command.tokens.clear();
                return;
            } else {
                command.hasRedirection = true; 
                command.redirFile = command.tokens.at(i + 1);
                command.tokens.pop_back();
                command.tokens.pop_back();
                return;
            }
        }
    }
}


void addToken(std::vector<char*>& tokens, const std::string& token) {
    if (!token.empty()) {
        char* temp = new char[token.size() + 1];
        std::strcpy(temp, token.c_str());
        tokens.push_back(temp);
    }
}

std::vector<Command> parseParallel(const std::string& input) {
    std::vector<Command> commands;
    Command currCommand;
    std::string temp;

    for (char c : input) {
        if (c == ' ') {
                addToken(currCommand.tokens, temp);
                temp.clear();
        } else if (c == *REDIR) {
                addToken(currCommand.tokens, temp);
                temp.clear();
            addToken(currCommand.tokens, std::string(REDIR));
        } else if (c == *PARALLEL) {
                addToken(currCommand.tokens, temp);
                temp.clear();
            if (!currCommand.tokens.empty()) { 
                commands.push_back(currCommand);
                currCommand = Command(); // reset for new command
            }
        } else {
            temp += c;
        }
    }
    addToken(currCommand.tokens, temp);
    if (!currCommand.tokens.empty()) { 
        commands.push_back(currCommand);
    }
    return commands;
}


void process_command(const std::string& input, std::vector<std::string>& paths) {
    if (input.empty()) return;
    if (input == EXIT) exit(0); 

    std::vector<Command> Commands = parseParallel(input);
    std::vector<pid_t> childPid;
    
    for (auto& command : Commands) {
        if (command.tokens.empty() || command.tokens.at(0) == nullptr) {
            freeMem(command.tokens);
        } else if (strcmp(command.tokens.at(0), BUILTIN_PATH) == 0) { // set paths
            paths = set_paths(command.tokens);
        } else if (strcmp(command.tokens.at(0), BUILTIN_CD) == 0) {
            if (command.tokens.size() != 2) {
                error();
                return;
            }
            if (chdir(command.tokens.at(1)) != 0) { // if cd failed
                error();
            }
        } else {
            parseRedir(command);
            command.tokens.push_back(nullptr);

            pid_t pid = fork();
            if (pid == 0) { // child
                child(command, paths);
                exit(0);
            } else if (pid > 0) { // parent
                childPid.push_back(pid);
            } else {
                error();
            }
            freeMem(command.tokens);
        }   
    }

    for (const pid_t pid : childPid) { // wait all
        waitpid(pid, NULL, 0);
    }
    for (auto& command : Commands) {
        freeMem(command.tokens);
    }
}

void batch_mode(const char* filename) {
    std::ifstream file(filename);
    if (!file) {
        error();
        exit(1);
    }

    std::vector<std::string> paths = {PATH};
    std::string line;

    while (std::getline(file, line)) {
        process_command(line, paths);
    }
    file.close();
}

void interactive_mode() {
    std::string input;
    std::vector<std::string> paths = {PATH};

    while(true) {
        write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
        std::getline(std::cin, input);
        process_command(input, paths);
    }
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        interactive_mode();    
    } else if (argc == 2) {
        batch_mode(argv[1]);
        exit(0);
    } else {
        error();
        exit(1);
    }
    return 0;
}
