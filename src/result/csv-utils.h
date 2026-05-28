#ifndef SIM_SRC_RESULT_CSV_UTILS_H
#define SIM_SRC_RESULT_CSV_UTILS_H

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

inline std::string
csvEscape(const std::string& value)
{
    const bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes)
    {
        return value;
    }

    std::ostringstream escaped;
    escaped << '"';
    for (const char ch : value)
    {
        if (ch == '"')
        {
            escaped << "\"\"";
        }
        else
        {
            escaped << ch;
        }
    }
    escaped << '"';
    return escaped.str();
}

inline std::string
csvBool(bool value)
{
    return value ? std::string("true") : std::string("false");
}

template <typename Value>
std::string
csvValue(const Value& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

inline void
writeCsvRow(std::ofstream& file, const std::vector<std::string>& values)
{
    for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex)
    {
        if (valueIndex > 0)
        {
            file << ",";
        }
        file << csvEscape(values[valueIndex]);
    }
    file << "\n";
}

inline std::string
joinCsvPath(const std::string& directory, const std::string& fileName)
{
    if (directory.empty() || directory[directory.size() - 1] == '/')
    {
        return directory + fileName;
    }
    return directory + "/" + fileName;
}

inline std::ofstream
OpenStructuredResultCsv(const std::string& path, bool& exportSuccess)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        exportSuccess = false;
        std::cout << "[HYBRID-DCN][EXPORT] error = failed-to-open:" << path << std::endl;
    }
    return file;
}

inline std::ofstream
OpenStructuredResultCsvWithHeader(const std::string& path,
                                  const std::vector<std::string>& header,
                                  bool& exportSuccess)
{
    std::ofstream file = OpenStructuredResultCsv(path, exportSuccess);
    if (file.is_open())
    {
        writeCsvRow(file, header);
    }
    return file;
}

#endif
