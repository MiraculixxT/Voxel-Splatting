#include "Application.hpp"
#include <iostream>

int main() {
    Application app;

    try {
        std::cout << "Initializing application..." << std::endl;
        app.Init();
        std::cout << "Running application..." << std::endl;
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return -1;
    }

    std::cout << "Application exited successfully." << std::endl;
    return 0;
}