#include "Application.h"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[])
{
    Application application{};
    if (argc >= 2)
    {
        return application.run(std::filesystem::path{argv[1]});
    }

    return application.run();
}
