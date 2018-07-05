﻿
#include <filesystem>
#include <map>
#include <vector>

#include <test_config.h>

#include "attributes.h"
#include "common_paths.h"

using namespace std::literals;

template <typename Func>
static int DoEnumerate(const wchar_t* pattern, Func&& func)
{
    WIN32_FIND_DATAW data;
    auto findHandle = ::FindFirstFileW(pattern, &data);
    if (findHandle == INVALID_HANDLE_VALUE)
    {
        if (auto err = ::GetLastError();  err == ERROR_FILE_NOT_FOUND)
        {
            return ERROR_SUCCESS;
        }
        else
        {
            return trace_error(err, L"FindFirstFile failed");
        }
    }

    int result = ERROR_SUCCESS;
    while (true)
    {
        result = func(data);
        if (result)
        {
            break;
        }
        else if (!::FindNextFileW(findHandle, &data))
        {
            if (auto err = ::GetLastError(); err != ERROR_NO_MORE_FILES)
            {
                result = trace_error(err, L"FindNextFile failed");
            }

            break;
        }
    }

    ::FindClose(findHandle);
    return result;
}

static int DoEnumerateTest(std::filesystem::path directory, std::map<std::wstring, DWORD> expectedContents)
{
    trace_messages(L"Enumerating the directory: ", info_color, directory.native(), new_line);

    auto result = DoEnumerate((directory.native() + L"/*").c_str(), [&](WIN32_FIND_DATAW& data) -> int
    {
        // Ignore "." and ".."
        if (data.cFileName == L"."sv || data.cFileName == L".."sv)
        {
            return ERROR_SUCCESS;
        }

        auto itr = expectedContents.find(data.cFileName);
        if (itr == expectedContents.end())
        {
            trace_messages(error_color, L"ERROR: Unexpected item found: ", error_info_color, data.cFileName, new_line);
            return ERROR_ASSERTION_FAILURE;
        }

        trace_messages(L"    Found: ", info_color, data.cFileName);

        auto attr = ::GetFileAttributesW((directory / data.cFileName).c_str());
        trace_messages(L" (", info_color, file_attributes(attr), console::color::gray, L")\n");

        if (attr != itr->second)
        {
            trace_message(L"ERROR: Attributes did not match the expected value\n", error_color);
            trace_messages(error_color, L"ERROR: Expected value: ", error_info_color, file_attributes(itr->second), new_line);
            return ERROR_ASSERTION_FAILURE;
        }

        expectedContents.erase(itr);
        return ERROR_SUCCESS;
    });
    if (result) return result;

    if (!expectedContents.empty())
    {
        trace_message(L"ERROR: Expected to find directories: ", error_color);
        const wchar_t* prefix = L"";
        for (auto& contents : expectedContents)
        {
            trace_messages(prefix, error_info_color, contents.first);
        }
        trace_message(L"\n");
        return ERROR_ASSERTION_FAILURE;
    }

    return ERROR_SUCCESS;
}

int EnumerateDirectoriesTests()
{
    int result = ERROR_SUCCESS;

    clean_redirection_path();

    // There should be three directories under "Tèƨƭ" - Â, ß, and Ç
    std::map<std::wstring, DWORD> expect =
    {
        { L"Â"s, FILE_ATTRIBUTE_DIRECTORY },
        { L"ß"s, FILE_ATTRIBUTE_DIRECTORY },
        { L"Ç"s, FILE_ATTRIBUTE_DIRECTORY },
    };

    test_begin("Enumerate Existing Package Directories Test");
    auto testResult = DoEnumerateTest(L"Tèƨƭ", expect);
    result = result ? result : testResult;
    test_end(testResult);

    // Create a new directory that we should find when enumerating
    test_begin("Find New Directory Test");
    testResult = [&]()
    {
        trace_messages(L"Creating a new directory \"", info_color, L"Ð", console::color::gray, L"\" that we should find\n");
        if (!::CreateDirectoryW(L"Tèƨƭ/Ð", nullptr))
        {
            return trace_last_error(L"Failed to create the new directory");
        }
        expect.insert({ L"Ð"s, FILE_ATTRIBUTE_DIRECTORY });
        return DoEnumerateTest(L"Tèƨƭ", expect);
    }();
    result = result ? result : testResult;
    test_end(testResult);

    // Modify a directory's attributes, which should also copy it to the redirected location
    test_begin("Modified Directory Attributes Enumeration Test");
    testResult = [&]()
    {
        trace_message(L"Modifying Â's attributes to include FILE_ATTRIBUTE_HIDDEN\n");
        if (!::SetFileAttributesW(L"Tèƨƭ/Â", FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN))
        {
            return trace_last_error(L"Failed to set Â's attributes");
        }
        expect.find({ L"Â" })->second |= FILE_ATTRIBUTE_HIDDEN;
        return DoEnumerateTest(L"Tèƨƭ", expect);
    }();
    result = result ? result : testResult;
    test_end(testResult);

    // We should be case-insensitive
    test_begin("Case-Insensitivity Directory Enumeration Test");
    trace_messages(L"Trying to create the directory \"", info_color, L"ç", console::color::gray, L"\" but this time lowercase\n");
    if (::CreateDirectoryW(L"Tèƨƭ/ç", nullptr)) // Best effort
    {
        // NOTE: We'll end up finding the lower-case one, so swap them out
        expect.erase(L"Ç"s);
        expect[L"ç"s] = FILE_ATTRIBUTE_DIRECTORY;
    }

    testResult = DoEnumerateTest(L"Tèƨƭ", expect);
    result = result ? result : testResult;
    test_end(testResult);

    return result;
}
