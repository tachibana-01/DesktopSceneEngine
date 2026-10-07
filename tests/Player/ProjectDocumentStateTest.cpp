#include "ProjectDocumentState.h"

#include <filesystem>
#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

}

int main()
{
    DesktopSceneEngine::Player::ProjectDocumentState state{};
    state.cleanRevision = 7U;

    if (!require(!state.isDirty(7U),
            "Equal authored and clean revisions must be clean")
        || !require(state.isDirty(8U),
            "A newer authored revision must be dirty"))
    {
        return 1;
    }

    state.markClean(8U);
    if (!require(!state.isDirty(8U),
            "markClean must establish the new clean revision"))
    {
        return 1;
    }

    state.currentPath = std::filesystem::path{"C:/Projects/example.dseproject"};
    if (!require(!state.isDirty(8U),
            "Changing path alone must not alter dirty calculation"))
    {
        return 1;
    }

    state.markClean(8U);
    if (!require(!state.isDirty(8U),
            "markClean must preserve clean revision state")
        || !require(state.currentPath.has_value(),
            "markClean must not alter the current document path"))
    {
        return 1;
    }

    state.currentPath.reset();
    return require(!state.isDirty(8U),
        "Changing current path alone must not alter dirty calculation") ? 0 : 1;
}
